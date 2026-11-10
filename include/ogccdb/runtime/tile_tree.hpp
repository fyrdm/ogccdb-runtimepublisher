#pragma once
// The CDB 1.3 tile tree that the runtime walks: per geocell, a chain of one
// tile per negative LOD, then a quadtree from LOD 0 down. Pure functions and
// an in-memory index of the tiles a store holds; no I/O.
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

#include "ogccdb/cdb13/tile.hpp"
#include "ogccdb/cdb13/tile_path.hpp"

namespace ogccdb::runtime {

using cdb13::GeoExtent;
using cdb13::Geocell;
using cdb13::TileAddress;

struct TileAddressHash {
    std::size_t operator()(const TileAddress& t) const noexcept {
        std::size_t h = static_cast<std::size_t>(t.geocell.lat + 90);
        h = h * 361u + static_cast<std::size_t>(t.geocell.lon + 180);
        h = h * 64u + static_cast<std::size_t>(t.lod + 10);
        h ^= std::hash<long long>{}((static_cast<long long>(t.u) << 32) ^ t.r) + 0x9e3779b97f4a7c15ull +
             (h << 6) + (h >> 2);
        return h;
    }
};

using TileSet = std::unordered_set<TileAddress, TileAddressHash>;

/// Tiles at LOD t.lod + 1 that `t` covers, the inverse of cdb13::parent_tile()
/// (CDB 1.3 Vol 1 §7.1.2.3 Requirement 16): the single tile of the next LOD for
/// a negative LOD, the four tiles (2U..2U+1, 2R..2R+1) for LOD 0 and finer.
/// Empty at the finest LOD. Throws std::invalid_argument if `t` is not valid.
std::vector<TileAddress> child_tiles(const TileAddress& t);

/// The coarsest tile (LOD -10) of a geocell: the root of its tile tree.
TileAddress root_tile(Geocell g);

/// True if `ancestor` is `t` or covers it at a coarser LOD.
bool covers(const TileAddress& ancestor, const TileAddress& t);

/// Which Elevation and Imagery tiles a store holds, and the tree they span.
class TileIndex {
public:
    TileIndex() = default;

    /// Adds a tile of `dataset` (cdb13::kElevation or cdb13::kImagery); other
    /// datasets are ignored. Its ancestors up to LOD -10 join the tree.
    void add(int dataset, const TileAddress& t);

    bool has_elevation(const TileAddress& t) const { return elevation_.count(t) != 0; }
    bool has_imagery(const TileAddress& t) const { return imagery_.count(t) != 0; }
    /// True if `t` holds a tile or is an ancestor of one.
    bool in_tree(const TileAddress& t) const { return tree_.count(t) != 0; }

    /// The finest tile at or above `t` (t itself, its parent, ...) that holds
    /// Elevation, or Imagery; nullopt if none does.
    std::optional<TileAddress> elevation_source(const TileAddress& t) const;
    std::optional<TileAddress> imagery_source(const TileAddress& t) const;

    /// True if a render tile at `t` has any data to show (see *_source()).
    bool has_data(const TileAddress& t) const {
        return elevation_source(t).has_value() || imagery_source(t).has_value();
    }

    /// LOD -10 tiles of the geocells that hold any tile, sorted south-west first.
    const std::vector<TileAddress>& roots() const { return roots_; }
    /// Union of the extents of the geocells that hold any tile; nullopt if empty.
    std::optional<GeoExtent> extent() const;

    std::size_t elevation_count() const { return elevation_.size(); }
    std::size_t imagery_count() const { return imagery_.size(); }
    /// Finest LOD that holds a tile of either dataset; nullopt if empty.
    std::optional<int> finest_lod() const { return finest_lod_; }

private:
    TileSet elevation_;
    TileSet imagery_;
    TileSet tree_;
    std::vector<TileAddress> roots_;
    std::optional<int> finest_lod_;
};

}  // namespace ogccdb::runtime
