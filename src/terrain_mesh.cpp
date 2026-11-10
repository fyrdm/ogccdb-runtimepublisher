#include "ogccdb/runtime/terrain_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/runtime/geodesy.hpp"
#include "ogccdb/runtime/tile_tree.hpp"

namespace ogccdb::runtime {

double sample_height(const cdb13::ElevationTile& elevation, double lat, double lon) {
    const int n = cdb13::raster_size(elevation.tile.lod);
    if (n <= 0 || elevation.elevation.size() != static_cast<std::size_t>(n) * n) return 0.0;
    const GeoExtent e = cdb13::extent(elevation.tile);
    // CDB 1.3 Vol 1 §10.6.1: post (i, j) at (west + i * dx, north - j * dy).
    const double dx = (e.east - e.west) / n, dy = (e.north - e.south) / n;
    const double x = std::clamp((lon - e.west) / dx, 0.0, static_cast<double>(n - 1));
    const double y = std::clamp((e.north - lat) / dy, 0.0, static_cast<double>(n - 1));
    const int i0 = static_cast<int>(std::floor(x)), j0 = static_cast<int>(std::floor(y));
    const int i1 = std::min(i0 + 1, n - 1), j1 = std::min(j0 + 1, n - 1);
    const double fx = x - i0, fy = y - j0;
    auto at = [&](int i, int j) { return static_cast<double>(elevation.elevation[static_cast<std::size_t>(j) * n + i]); };
    const double top = at(i0, j0) * (1.0 - fx) + at(i1, j0) * fx;
    const double bottom = at(i0, j1) * (1.0 - fx) + at(i1, j1) * fx;
    return top * (1.0 - fy) + bottom * fy;
}

TerrainMesh build_terrain_mesh(const cdb13::TileAddress& tile, const cdb13::ElevationTile* elevation,
                               const MeshOptions& options) {
    if (!cdb13::is_valid(tile)) throw std::invalid_argument("build_terrain_mesh: invalid tile address");
    if (options.resolution < 1) throw std::invalid_argument("build_terrain_mesh: resolution must be >= 1");
    if (elevation && !covers(elevation->tile, tile)) {
        throw std::invalid_argument("build_terrain_mesh: elevation tile does not cover the tile");
    }
    const int res = options.resolution;
    const int side = res + 1;
    const GeoExtent e = cdb13::extent(tile);
    const double clat = (e.south + e.north) / 2.0, clon = (e.west + e.east) / 2.0;

    TerrainMesh m;
    m.center = geodetic_to_ecef({clat, clon, 0.0});
    m.up = ellipsoid_up(clat, clon);

    // Grid positions in double precision first, for the normals.
    std::vector<Vec3d> p(static_cast<std::size_t>(side) * side);
    std::vector<double> h(p.size());
    for (int j = 0; j < side; ++j) {
        const double lat = e.north - (e.north - e.south) * j / res;
        for (int i = 0; i < side; ++i) {
            const double lon = e.west + (e.east - e.west) * i / res;
            const double height = elevation ? sample_height(*elevation, lat, lon) : 0.0;
            const std::size_t k = static_cast<std::size_t>(j) * side + i;
            h[k] = height;
            p[k] = geodetic_to_ecef({lat, lon, height});
        }
    }
    auto P = [&](int i, int j) {
        return p[static_cast<std::size_t>(std::clamp(j, 0, res)) * side + std::clamp(i, 0, res)];
    };

    const std::size_t grid_vertices = p.size();
    const std::size_t skirt_vertices = static_cast<std::size_t>(4 * res);
    const std::size_t total = grid_vertices + skirt_vertices;
    m.positions.reserve(total * 3);
    m.normals.reserve(total * 3);
    m.uvs.reserve(total * 2);
    m.heights.reserve(total);

    auto push = [&](Vec3d pos, Vec3d normal, double u, double v, double height) {
        const Vec3d rel = pos - m.center;
        m.positions.insert(m.positions.end(), {static_cast<float>(rel.x), static_cast<float>(rel.y),
                                               static_cast<float>(rel.z)});
        m.normals.insert(m.normals.end(), {static_cast<float>(normal.x), static_cast<float>(normal.y),
                                           static_cast<float>(normal.z)});
        m.uvs.insert(m.uvs.end(), {static_cast<float>(u), static_cast<float>(v)});
        m.heights.push_back(static_cast<float>(height));
    };

    std::vector<Vec3d> normals(grid_vertices);
    double hmin = h.front(), hmax = h.front();
    for (int j = 0; j < side; ++j) {
        for (int i = 0; i < side; ++i) {
            // East tangent x north tangent (north = -south) is up.
            const Vec3d east = P(i + 1, j) - P(i - 1, j);
            const Vec3d south = P(i, j + 1) - P(i, j - 1);
            Vec3d n = normalize(cross(south, east));
            if (length(n) == 0.0) n = m.up;
            const std::size_t k = static_cast<std::size_t>(j) * side + i;
            normals[k] = n;
            hmin = std::min(hmin, h[k]);
            hmax = std::max(hmax, h[k]);
            push(p[k], n, static_cast<double>(i) / res, static_cast<double>(j) / res, h[k]);
        }
    }
    m.min_height = static_cast<float>(hmin);
    m.max_height = static_cast<float>(hmax);

    auto idx = [&](int i, int j) { return static_cast<std::uint32_t>(j * side + i); };
    m.indices.reserve(static_cast<std::size_t>(res) * res * 6 + skirt_vertices * 6);
    for (int j = 0; j < res; ++j) {
        for (int i = 0; i < res; ++i) {
            const std::uint32_t a = idx(i, j), b = idx(i + 1, j), c = idx(i, j + 1), d = idx(i + 1, j + 1);
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }

    // Skirt: the edge ring walked clockwise, each vertex copied and lowered.
    const double ns_metres = (e.north - e.south) * kDegToRad * kMeanEarthRadius;
    const double depth = ns_metres * options.skirt_fraction + options.skirt_min;
    std::vector<std::pair<int, int>> ring;
    ring.reserve(skirt_vertices);
    for (int i = 0; i < res; ++i) ring.emplace_back(i, 0);          // north edge, west to east
    for (int j = 0; j < res; ++j) ring.emplace_back(res, j);        // east edge, north to south
    for (int i = res; i > 0; --i) ring.emplace_back(i, res);        // south edge, east to west
    for (int j = res; j > 0; --j) ring.emplace_back(0, j);          // west edge, south to north
    const std::uint32_t first_skirt = static_cast<std::uint32_t>(grid_vertices);
    for (const auto& [i, j] : ring) {
        const std::size_t k = static_cast<std::size_t>(j) * side + i;
        push(p[k] - ellipsoid_up(e.north - (e.north - e.south) * j / res,
                                 e.west + (e.east - e.west) * i / res) * depth,
             normals[k], static_cast<double>(i) / res, static_cast<double>(j) / res, h[k]);
    }
    const std::uint32_t ring_size = static_cast<std::uint32_t>(ring.size());
    for (std::uint32_t s = 0; s < ring_size; ++s) {
        const std::uint32_t s1 = (s + 1) % ring_size;
        const std::uint32_t top0 = idx(ring[s].first, ring[s].second);
        const std::uint32_t top1 = idx(ring[s1].first, ring[s1].second);
        const std::uint32_t bot0 = first_skirt + s, bot1 = first_skirt + s1;
        m.indices.insert(m.indices.end(), {top0, top1, bot0, top1, bot1, bot0});
    }
    return m;
}

Image crop_imagery(const cdb13::TileAddress& tile, const cdb13::ImageryTile& imagery) {
    if (!cdb13::is_valid(tile)) throw std::invalid_argument("crop_imagery: invalid tile address");
    if (!covers(imagery.tile, tile)) {
        throw std::invalid_argument("crop_imagery: imagery tile does not cover the tile");
    }
    const int n = cdb13::raster_size(imagery.tile.lod);
    const int ch = imagery.channels;
    if ((ch != 1 && ch != 3) || imagery.pixels.size() != static_cast<std::size_t>(n) * n * ch) {
        throw std::invalid_argument("crop_imagery: imagery tile has a wrong pixel count");
    }
    const GeoExtent src = cdb13::extent(imagery.tile);
    const GeoExtent dst = cdb13::extent(tile);
    auto px = [&](double lon) { return static_cast<int>(std::lround((lon - src.west) / (src.east - src.west) * n)); };
    auto py = [&](double lat) { return static_cast<int>(std::lround((src.north - lat) / (src.north - src.south) * n)); };
    int x0 = std::clamp(px(dst.west), 0, n - 1), y0 = std::clamp(py(dst.north), 0, n - 1);
    int x1 = std::clamp(px(dst.east), x0 + 1, n), y1 = std::clamp(py(dst.south), y0 + 1, n);

    Image img;
    img.width = x1 - x0;
    img.height = y1 - y0;
    img.rgb.resize(static_cast<std::size_t>(img.width) * img.height * 3);
    std::uint8_t* out = img.rgb.data();
    for (int y = y0; y < y1; ++y) {
        const std::uint8_t* row = imagery.pixels.data() + (static_cast<std::size_t>(y) * n + x0) * ch;
        if (ch == 3) {
            out = std::copy(row, row + static_cast<std::size_t>(img.width) * 3, out);
        } else {
            for (int x = 0; x < img.width; ++x) {
                *out++ = row[x];
                *out++ = row[x];
                *out++ = row[x];
            }
        }
    }
    return img;
}

}  // namespace ogccdb::runtime
