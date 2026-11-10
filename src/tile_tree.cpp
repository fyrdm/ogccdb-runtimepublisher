#include "ogccdb/runtime/tile_tree.hpp"

#include <algorithm>
#include <stdexcept>

#include "ogccdb/cdb13/lod.hpp"

namespace ogccdb::runtime {

std::vector<TileAddress> child_tiles(const TileAddress& t) {
    if (!cdb13::is_valid(t)) throw std::invalid_argument("child_tiles: invalid tile address");
    if (t.lod >= cdb13::kMaxLod) return {};
    // CDB 1.3 Vol 1 §7.1.2.3: a negative-LOD tile covers the whole geocell,
    // so its only child is the next LOD's single tile; from LOD 0 each tile
    // splits into 2 x 2 tiles of the next LOD.
    if (t.lod < 0) return {TileAddress{t.geocell, t.lod + 1, 0, 0}};
    std::vector<TileAddress> c;
    c.reserve(4);
    for (int du = 0; du < 2; ++du) {
        for (int dr = 0; dr < 2; ++dr) c.push_back({t.geocell, t.lod + 1, 2 * t.u + du, 2 * t.r + dr});
    }
    return c;
}

TileAddress root_tile(Geocell g) { return TileAddress{g, cdb13::kMinLod, 0, 0}; }

bool covers(const TileAddress& ancestor, const TileAddress& t) {
    if (ancestor.geocell != t.geocell || ancestor.lod > t.lod) return false;
    if (ancestor.lod < 0) return true;  // a negative-LOD tile covers the whole geocell
    const int shift = t.lod - ancestor.lod;
    return (t.u >> shift) == ancestor.u && (t.r >> shift) == ancestor.r;
}

void TileIndex::add(int dataset, const TileAddress& t) {
    if (!cdb13::is_valid(t)) return;
    if (dataset == cdb13::kElevation) {
        elevation_.insert(t);
    } else if (dataset == cdb13::kImagery) {
        imagery_.insert(t);
    } else {
        return;
    }
    finest_lod_ = std::max(finest_lod_.value_or(cdb13::kMinLod), t.lod);
    std::optional<TileAddress> a = t;
    while (a) {
        if (!tree_.insert(*a).second) break;  // the rest of the chain is already there
        if (a->lod == cdb13::kMinLod) {
            roots_.push_back(*a);
            std::sort(roots_.begin(), roots_.end(), [](const TileAddress& x, const TileAddress& y) {
                return x.geocell.lat != y.geocell.lat ? x.geocell.lat < y.geocell.lat
                                                      : x.geocell.lon < y.geocell.lon;
            });
        }
        a = cdb13::parent_tile(*a);
    }
}

namespace {
std::optional<TileAddress> finest_at_or_above(const TileSet& set, const TileAddress& t) {
    if (set.empty() || !cdb13::is_valid(t)) return std::nullopt;
    std::optional<TileAddress> a = t;
    while (a) {
        if (set.count(*a)) return a;
        a = cdb13::parent_tile(*a);
    }
    return std::nullopt;
}
}  // namespace

std::optional<TileAddress> TileIndex::elevation_source(const TileAddress& t) const {
    return finest_at_or_above(elevation_, t);
}

std::optional<TileAddress> TileIndex::imagery_source(const TileAddress& t) const {
    return finest_at_or_above(imagery_, t);
}

std::optional<GeoExtent> TileIndex::extent() const {
    if (roots_.empty()) return std::nullopt;
    GeoExtent e = cdb13::extent(roots_.front().geocell);
    for (const TileAddress& r : roots_) {
        const GeoExtent g = cdb13::extent(r.geocell);
        e.south = std::min(e.south, g.south);
        e.west = std::min(e.west, g.west);
        e.north = std::max(e.north, g.north);
        e.east = std::max(e.east, g.east);
    }
    return e;
}

}  // namespace ogccdb::runtime
