#pragma once
// Streaming terrain over a CDB 1.3 store: picks the tiles a camera needs,
// loads their Elevation and Imagery on worker threads and turns them into
// engine-neutral meshes and textures.
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ogccdb/cdb13/imagery.hpp"
#include "ogccdb/runtime/selection.hpp"
#include "ogccdb/runtime/terrain_mesh.hpp"
#include "ogccdb/runtime/tile_tree.hpp"

namespace ogccdb::runtime {

struct TerrainOptions {
    MeshOptions mesh;
    SelectionOptions selection;
    /// Imagery component to show (CDB 1.3 Vol 1 §10.6.2.2 Table 5-15).
    cdb13::ImageryComponent imagery = cdb13::kYearlyVsti;
    /// Worker threads that read and build tiles; 0 picks from the CPU count.
    int worker_threads = 0;
    /// Memory for built render tiles; tiles not drawn lately are dropped above it.
    std::size_t tile_budget_bytes = std::size_t{768} << 20;
    /// Memory for decoded source tiles shared by render tiles.
    std::size_t source_budget_bytes = std::size_t{384} << 20;
};

/// A render tile, ready to upload: its mesh and its texture (empty if the
/// store has no imagery over it).
struct TerrainTile {
    TileAddress tile;
    std::optional<TileAddress> elevation_source;  ///< tile its heights come from
    std::optional<TileAddress> imagery_source;    ///< tile its texture comes from
    TerrainMesh mesh;
    Image image;

    std::size_t memory_bytes() const;
};

struct DrawTile {
    TileAddress tile;
    std::shared_ptr<const TerrainTile> data;
};

struct TerrainStats {
    std::uint64_t frame = 0;
    std::size_t drawn = 0;
    std::size_t culled = 0;
    std::size_t queued = 0;      ///< requests waiting for a worker
    std::size_t loading = 0;     ///< requests being built
    std::size_t resident = 0;    ///< built render tiles kept in memory
    std::size_t tile_bytes = 0;
    std::size_t source_bytes = 0;
    std::size_t loaded_total = 0;
    std::size_t failed_total = 0;
    std::vector<int> drawn_per_lod;  ///< index lod - kMinLod
    std::string last_error;
};

class Terrain {
public:
    /// Opens the CDB Version at `cdb_root` and indexes its Elevation and Imagery
    /// tiles. Throws std::runtime_error if it is not a CDB Version (see
    /// cdb13::Store::open()).
    explicit Terrain(const std::filesystem::path& cdb_root, TerrainOptions options = {});
    ~Terrain();
    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    const TileIndex& index() const;
    const TerrainOptions& options() const;
    /// Changes the selection options from the next update().
    void set_selection_options(const SelectionOptions& s);

    /// Takes in finished tiles, selects the tiles to draw for `camera` and
    /// queues the ones still missing. Call once per frame from one thread.
    /// The returned list stays valid until the next call.
    const std::vector<DrawTile>& update(const Camera& camera);

    TerrainStats stats() const;

    /// Blocks until no tile is queued or loading (for tests and screenshots).
    void wait_idle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogccdb::runtime
