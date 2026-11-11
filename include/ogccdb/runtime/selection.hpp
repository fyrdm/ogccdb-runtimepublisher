#pragma once
// View-dependent choice of which render tiles to draw and which to load next.
// Pure: no I/O, no threads. The caller says which tiles are ready.
#include <functional>
#include <optional>
#include <vector>

#include "ogccdb/runtime/math.hpp"
#include "ogccdb/runtime/tile_tree.hpp"

namespace ogccdb::runtime {

/// A perspective camera in ECEF metres.
struct Camera {
    Vec3d position;
    Vec3d direction{0, 0, -1};  ///< view direction (need not be unit length)
    Vec3d up{0, 1, 0};
    double fov_y = 0.8;          ///< vertical field of view, radians
    double aspect = 16.0 / 9.0;  ///< width / height
    int viewport_height = 1080;  ///< pixels
    double near_plane = 1.0;
    double far_plane = 1.0e7;

    Mat4d view() const { return look_at_direction(position, direction, up); }
    Mat4d projection() const { return perspective(fov_y, aspect, near_plane, far_plane); }
};

/// Heights (metres above the ellipsoid) a tile spans.
struct HeightRange {
    double min = -500.0;
    double max = 9000.0;
};

struct SelectionOptions {
    /// Refine a tile while one of its texels (one data-grid spacing) covers
    /// more than this many pixels on screen.
    double max_screen_error = 1.5;
    /// Height range assumed for a tile until it or an ancestor is ready.
    HeightRange default_heights;
    /// Do not refine beyond this LOD.
    int max_lod = 23;
    /// LODs to refine past the store's finest tile, drawing finer meshes from
    /// the finest data (upsampled), so close views get smooth relief.
    int extra_lods = 3;
};

/// One tile the caller should load, with a priority (higher first).
struct TileRequest {
    TileAddress tile;
    double priority = 0.0;
};

struct Selection {
    std::vector<TileAddress> draw;       ///< ready tiles that together cover the view, no overlaps
    std::vector<TileRequest> requests;   ///< tiles to load, not ready yet
    /// Ready tiles the walk went through on the way to `draw` (refined
    /// ancestors). They must stay loaded: dropping one makes its whole
    /// subtree fall back to a coarser tile until it is loaded again.
    std::vector<TileAddress> refined;
    int culled = 0;                      ///< tiles skipped as outside the view or behind the horizon
};

/// Returns the height range of a ready tile, or nullopt if it is not ready.
using ReadyFn = std::function<std::optional<HeightRange>(const TileAddress&)>;

/// Walks the tile tree of `index` from its roots. A tile is refined into its
/// children while it is coarser than the screen needs and all its visible
/// children are ready; until then it stays drawn and its children are
/// requested. Children with no tile of their own are drawn from their
/// ancestors' data (see TileIndex::elevation_source()), so a refined tile
/// leaves no holes. Tiles with no data at all are never drawn.
Selection select_tiles(const TileIndex& index, const Camera& camera, const ReadyFn& ready,
                       const SelectionOptions& options = {});

/// Screen-space size in pixels of one data-grid spacing of `tile` seen from
/// `distance` metres (CDB 1.3 Vol 1 §7.1.2: raster_size(lod) elements per tile side).
double screen_error(const TileAddress& tile, double distance, const Camera& camera);

/// Bounding sphere of `tile` between two heights.
struct BoundingSphere {
    Vec3d center;
    double radius = 0.0;
};
BoundingSphere bounding_sphere(const TileAddress& tile, const HeightRange& heights);

/// Distance from `point` (ECEF) to the nearest point of `tile` between two heights,
/// measured to its closest geodetic position; 0 if the point is inside.
double distance_to_tile(Vec3d point, const TileAddress& tile, const HeightRange& heights);

/// True if a sphere is wholly hidden behind the earth's horizon as seen from `eye`.
bool below_horizon(Vec3d eye, const BoundingSphere& s);

}  // namespace ogccdb::runtime
