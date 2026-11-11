#pragma once
// Engine-neutral terrain tile geometry and texture, built from CDB 1.3
// Elevation and Imagery tiles. Pure functions, no I/O.
#include <cstdint>
#include <vector>

#include "ogccdb/cdb13/elevation.hpp"
#include "ogccdb/cdb13/imagery.hpp"
#include "ogccdb/runtime/math.hpp"

namespace ogccdb::runtime {

/// A regular grid mesh over one tile, plus a skirt around its edges that
/// hides cracks between neighbours at different LODs. Positions are ECEF
/// metres relative to `center`, so they fit in 32-bit floats; add `center`
/// (in double precision, or relative to the camera) to place the tile.
struct TerrainMesh {
    Vec3d center;                    ///< ECEF of the tile centre at height 0
    Vec3d up;                        ///< ellipsoid normal at the tile centre
    std::vector<float> positions;    ///< x, y, z per vertex
    std::vector<float> normals;      ///< x, y, z per vertex (unit surface normal)
    std::vector<float> uvs;          ///< u, v per vertex; (0, 0) is the north-west corner
    std::vector<float> heights;      ///< metres above the ellipsoid per vertex (skirts: edge height)
    std::vector<std::uint32_t> indices;  ///< triangles
    float min_height = 0.0f;
    float max_height = 0.0f;

    std::size_t vertex_count() const { return heights.size(); }
};

/// An RGB image, row by row from the north-west corner, 3 bytes per pixel.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;

    bool empty() const { return width == 0 || height == 0; }
};

struct MeshOptions {
    int resolution = 32;  ///< grid cells per tile side (vertices: resolution + 1)
    bool skirts = true;   ///< false: the bare surface only (shows cracks between LODs)
    /// Skirt depth below the tile edge, as a fraction of the tile's north-south size, plus `skirt_min` metres.
    double skirt_fraction = 0.01;
    double skirt_min = 5.0;
};

/// Height at (lat, lon) from `elevation`, by bilinear interpolation between its
/// posts. Posts sit on the tile grid corners, post (0, 0) on the north-west
/// corner (CDB 1.3 Vol 1 §10.6.1), so the strip along the east and south edges
/// past the last post takes the nearest post (the neighbour tile owns those edges).
/// Positions outside the tile are clamped to it.
double sample_height(const cdb13::ElevationTile& elevation, double lat, double lon);

/// Mesh of the render tile `tile`, with heights from `elevation`, which must
/// be `tile` or a tile covering it; heights are 0 without it.
/// Throws std::invalid_argument for an invalid tile, a resolution below 1,
/// or an elevation tile that does not cover `tile`.
TerrainMesh build_terrain_mesh(const cdb13::TileAddress& tile, const cdb13::ElevationTile* elevation,
                               const MeshOptions& options = {});

/// Texture of the render tile `tile`: the part of `imagery` (which must be
/// `tile` or a tile covering it) over `tile`, as RGB; gray-scale is expanded.
/// Imagery pixels sit in the grid cell centres (CDB 1.3 Vol 1 §10.6.2), so
/// texture coordinates 0..1 over the tile extent map them as given.
/// Throws std::invalid_argument for an invalid tile or an imagery tile that
/// does not cover `tile`.
Image crop_imagery(const cdb13::TileAddress& tile, const cdb13::ImageryTile& imagery);

}  // namespace ogccdb::runtime
