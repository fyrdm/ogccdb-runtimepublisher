#include "ogccdb/runtime/terrain.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <future>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/cdb13/store.hpp"

namespace ogccdb::runtime {

std::size_t TerrainTile::memory_bytes() const {
    return sizeof(*this) + image.rgb.size() +
           (mesh.positions.size() + mesh.normals.size() + mesh.uvs.size() + mesh.heights.size()) * sizeof(float) +
           mesh.indices.size() * sizeof(std::uint32_t);
}

namespace {

/// Decoded source tiles, shared by render tiles and loaded once even when
/// several workers ask at the same time; least recently used ones go first.
template <class T>
class SourceCache {
public:
    using Ptr = std::shared_ptr<const T>;
    using Loader = std::function<std::optional<T>(const TileAddress&)>;

    Ptr get(const TileAddress& t, const Loader& load) {
        std::shared_future<Ptr> f;
        std::promise<Ptr> p;
        bool owner = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            auto it = entries_.find(t);
            if (it != entries_.end()) {
                lru_.splice(lru_.begin(), lru_, it->second.lru);
                f = it->second.value;
            } else {
                owner = true;
                f = p.get_future().share();
                lru_.push_front(t);
                entries_.emplace(t, Entry{f, lru_.begin(), 0});
            }
        }
        if (!owner) return f.get();
        Ptr value;
        try {
            std::optional<T> loaded = load(t);
            if (loaded) value = std::make_shared<const T>(std::move(*loaded));
        } catch (...) {
            std::lock_guard<std::mutex> lock(m_);
            remove(t);
            p.set_exception(std::current_exception());
            throw;
        }
        p.set_value(value);
        std::lock_guard<std::mutex> lock(m_);
        auto it = entries_.find(t);
        if (it != entries_.end()) {
            it->second.bytes = size_of(value);
            bytes_ += it->second.bytes;
        }
        trim();
        return value;
    }

    void set_budget(std::size_t b) { budget_ = b; }
    std::size_t bytes() const {
        std::lock_guard<std::mutex> lock(m_);
        return bytes_;
    }

private:
    struct Entry {
        std::shared_future<Ptr> value;
        std::list<TileAddress>::iterator lru;
        std::size_t bytes;
    };
    static std::size_t size_of(const Ptr& v);

    void remove(const TileAddress& t) {
        auto it = entries_.find(t);
        if (it == entries_.end()) return;
        bytes_ -= it->second.bytes;
        lru_.erase(it->second.lru);
        entries_.erase(it);
    }
    void trim() {
        while (bytes_ > budget_ && !lru_.empty()) {
            const TileAddress oldest = lru_.back();
            auto it = entries_.find(oldest);
            if (it->second.bytes == 0) break;  // still loading
            remove(oldest);
        }
    }

    mutable std::mutex m_;
    std::unordered_map<TileAddress, Entry, TileAddressHash> entries_;
    std::list<TileAddress> lru_;
    std::size_t bytes_ = 0;
    std::size_t budget_ = 0;
};

template <>
std::size_t SourceCache<cdb13::ElevationTile>::size_of(const Ptr& v) {
    return sizeof(cdb13::ElevationTile) + (v ? v->elevation.size() * sizeof(float) : 0);
}
template <>
std::size_t SourceCache<cdb13::ImageryTile>::size_of(const Ptr& v) {
    return sizeof(cdb13::ImageryTile) + (v ? v->pixels.size() : 0);
}

struct Pending {
    double priority = 0.0;
    std::uint64_t frame = 0;
};

struct Resident {
    std::shared_ptr<const TerrainTile> data;
    std::uint64_t last_used = 0;
};

}  // namespace

struct Terrain::Impl {
    std::filesystem::path root;
    TerrainOptions options;
    TileIndex index;

    SourceCache<cdb13::ElevationTile> elevation_cache;
    SourceCache<cdb13::ImageryTile> imagery_cache;

    // Shared with the workers, under `m`.
    mutable std::mutex m;
    mutable std::condition_variable work_cv;
    mutable std::condition_variable idle_cv;
    std::unordered_map<TileAddress, Pending, TileAddressHash> pending;
    TileSet loading;
    std::vector<std::shared_ptr<const TerrainTile>> finished;
    std::uint64_t current_frame = 0;
    bool stop = false;
    std::size_t loaded_total = 0;
    std::size_t failed_total = 0;
    std::string last_error;

    // Main thread only.
    std::unordered_map<TileAddress, Resident, TileAddressHash> resident;
    std::size_t resident_bytes = 0;
    std::vector<DrawTile> draw;
    TerrainStats last_stats;

    std::vector<std::thread> workers;

    std::shared_ptr<const TerrainTile> build(const TileAddress& t) {
        auto tile = std::make_shared<TerrainTile>();
        tile->tile = t;
        tile->elevation_source = index.elevation_source(t);
        tile->imagery_source = index.imagery_source(t);
        std::shared_ptr<const cdb13::ElevationTile> elevation;
        if (tile->elevation_source) {
            elevation = elevation_cache.get(*tile->elevation_source, [&](const TileAddress& a) {
                return cdb13::read_elevation_tile(root, a);
            });
        }
        tile->mesh = build_terrain_mesh(t, elevation.get(), options.mesh);
        if (tile->imagery_source) {
            const cdb13::ImageryComponent c = options.imagery;
            auto imagery = imagery_cache.get(*tile->imagery_source, [&](const TileAddress& a) {
                return cdb13::read_imagery_tile(root, a, c);
            });
            if (imagery) tile->image = crop_imagery(t, *imagery);
        }
        return tile;
    }

    void worker_loop() {
        std::unique_lock<std::mutex> lock(m);
        for (;;) {
            work_cv.wait(lock, [&] { return stop || !pending.empty(); });
            if (stop) return;
            // Highest priority among requests from the last frame or two; drop older ones.
            auto best = pending.end();
            for (auto it = pending.begin(); it != pending.end();) {
                if (it->second.frame + 2 < current_frame) {
                    it = pending.erase(it);
                    continue;
                }
                if (best == pending.end() || it->second.priority > best->second.priority) best = it;
                ++it;
            }
            if (best == pending.end()) {
                idle_cv.notify_all();
                continue;
            }
            const TileAddress t = best->first;
            pending.erase(best);
            loading.insert(t);
            lock.unlock();

            std::shared_ptr<const TerrainTile> tile;
            std::string error;
            try {
                tile = build(t);
            } catch (const std::exception& e) {
                error = e.what();
            }
            if (!tile) {
                // Keep the tile drawable: build it without the source that failed.
                try {
                    auto fallback = std::make_shared<TerrainTile>();
                    fallback->tile = t;
                    fallback->mesh = build_terrain_mesh(t, nullptr, options.mesh);
                    tile = fallback;
                } catch (...) {
                }
            }

            lock.lock();
            loading.erase(t);
            if (tile) finished.push_back(tile);
            if (error.empty()) {
                ++loaded_total;
            } else {
                ++failed_total;
                last_error = error;
            }
            if (pending.empty() && loading.empty()) idle_cv.notify_all();
        }
    }

    std::optional<HeightRange> ready(const TileAddress& t) const {
        auto it = resident.find(t);
        if (it == resident.end()) return std::nullopt;
        return HeightRange{it->second.data->mesh.min_height, it->second.data->mesh.max_height};
    }

    void evict(std::uint64_t frame) {
        if (resident_bytes <= options.tile_budget_bytes) return;
        std::vector<std::pair<std::uint64_t, TileAddress>> candidates;
        for (const auto& [t, r] : resident) {
            if (r.last_used + 1 < frame) candidates.emplace_back(r.last_used, t);
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [used, t] : candidates) {
            if (resident_bytes <= options.tile_budget_bytes) break;
            auto it = resident.find(t);
            resident_bytes -= it->second.data->memory_bytes();
            resident.erase(it);
        }
    }
};

Terrain::Terrain(const std::filesystem::path& cdb_root, TerrainOptions options) : impl_(new Impl) {
    const cdb13::Store store = cdb13::Store::open(cdb_root);
    impl_->root = store.root();
    impl_->options = options;
    impl_->elevation_cache.set_budget(options.source_budget_bytes / 2);
    impl_->imagery_cache.set_budget(options.source_budget_bytes / 2);
    for (const cdb13::TileFile& f : store.list_tiles({cdb13::kElevation, std::nullopt, std::nullopt})) {
        if (f.selector1 == 1 && f.selector2 == 1) impl_->index.add(cdb13::kElevation, f.tile);  // primary terrain
    }
    for (const cdb13::TileFile& f : store.list_tiles({cdb13::kImagery, std::nullopt, std::nullopt})) {
        if (f.selector1 == options.imagery.selector1 && f.selector2 == options.imagery.selector2) {
            impl_->index.add(cdb13::kImagery, f.tile);
        }
    }
    int n = options.worker_threads;
    if (n <= 0) n = std::max(2, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    for (int i = 0; i < n; ++i) impl_->workers.emplace_back([this] { impl_->worker_loop(); });
}

Terrain::~Terrain() {
    {
        std::lock_guard<std::mutex> lock(impl_->m);
        impl_->stop = true;
    }
    impl_->work_cv.notify_all();
    for (std::thread& t : impl_->workers) t.join();
}

const TileIndex& Terrain::index() const { return impl_->index; }
const TerrainOptions& Terrain::options() const { return impl_->options; }
void Terrain::set_selection_options(const SelectionOptions& s) { impl_->options.selection = s; }

const std::vector<DrawTile>& Terrain::update(const Camera& camera) {
    Impl& d = *impl_;
    std::vector<std::shared_ptr<const TerrainTile>> finished;
    std::uint64_t frame = 0;
    {
        std::lock_guard<std::mutex> lock(d.m);
        frame = ++d.current_frame;
        finished.swap(d.finished);
    }
    for (auto& t : finished) {
        auto& r = d.resident[t->tile];
        if (r.data) d.resident_bytes -= r.data->memory_bytes();
        r.data = std::move(t);
        r.last_used = frame;
        d.resident_bytes += r.data->memory_bytes();
    }

    const Selection sel = select_tiles(
        d.index, camera, [&](const TileAddress& t) { return d.ready(t); }, d.options.selection);

    d.draw.clear();
    TerrainStats& s = d.last_stats;
    s.drawn_per_lod.assign(cdb13::kMaxLod - cdb13::kMinLod + 1, 0);
    for (const TileAddress& t : sel.refined) d.resident.at(t).last_used = frame;
    for (const TileAddress& t : sel.draw) {
        Resident& r = d.resident.at(t);
        r.last_used = frame;
        d.draw.push_back({t, r.data});
        ++s.drawn_per_lod[static_cast<std::size_t>(t.lod - cdb13::kMinLod)];
    }
    {
        std::lock_guard<std::mutex> lock(d.m);
        for (const TileRequest& q : sel.requests) {
            if (d.loading.count(q.tile)) continue;
            Pending& p = d.pending[q.tile];
            p.priority = q.priority;
            p.frame = frame;
        }
        s.queued = d.pending.size();
        s.loading = d.loading.size();
        s.loaded_total = d.loaded_total;
        s.failed_total = d.failed_total;
        s.last_error = d.last_error;
    }
    if (!sel.requests.empty()) d.work_cv.notify_all();
    d.evict(frame);

    s.frame = frame;
    s.drawn = d.draw.size();
    s.culled = static_cast<std::size_t>(sel.culled);
    s.resident = d.resident.size();
    s.tile_bytes = d.resident_bytes;
    s.source_bytes = d.elevation_cache.bytes() + d.imagery_cache.bytes();
    return d.draw;
}

TerrainStats Terrain::stats() const { return impl_->last_stats; }

void Terrain::wait_idle() const {
    std::unique_lock<std::mutex> lock(impl_->m);
    impl_->idle_cv.wait(lock, [&] {
        bool fresh = false;
        for (const auto& [t, p] : impl_->pending) {
            if (p.frame + 2 >= impl_->current_frame) fresh = true;
        }
        return !fresh && impl_->loading.empty();
    });
}

}  // namespace ogccdb::runtime
