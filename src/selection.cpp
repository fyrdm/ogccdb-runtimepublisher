#include "ogccdb/runtime/selection.hpp"

#include <algorithm>
#include <cmath>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/runtime/geodesy.hpp"

namespace ogccdb::runtime {

double screen_error(const TileAddress& tile, double distance, const Camera& camera) {
    // lat_spacing(): one geocell height over the raster elements along it
    // (CDB 1.3 Vol 1 §7.1.2, Table 2-4).
    const double texel = cdb13::lat_spacing(tile.lod) * kDegToRad * kMeanEarthRadius;
    const double k = camera.viewport_height / (2.0 * std::tan(camera.fov_y / 2.0));
    return texel * k / std::max(distance, 1.0);
}

BoundingSphere bounding_sphere(const TileAddress& tile, const HeightRange& heights) {
    const GeoExtent e = cdb13::extent(tile);
    std::vector<Vec3d> pts;
    pts.reserve(18);
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            const double lat = e.south + (e.north - e.south) * j / 2.0;
            const double lon = e.west + (e.east - e.west) * i / 2.0;
            pts.push_back(geodetic_to_ecef({lat, lon, heights.min}));
            pts.push_back(geodetic_to_ecef({lat, lon, heights.max}));
        }
    }
    Vec3d c;
    for (const Vec3d& p : pts) c += p;
    c = c / static_cast<double>(pts.size());
    double r = 0.0;
    for (const Vec3d& p : pts) r = std::max(r, length(p - c));
    return {c, r};
}

double distance_to_tile(Vec3d point, const TileAddress& tile, const HeightRange& heights) {
    const GeoExtent e = cdb13::extent(tile);
    const Geodetic g = ecef_to_geodetic(point);
    const Geodetic nearest{std::clamp(g.lat, e.south, e.north), std::clamp(g.lon, e.west, e.east),
                           std::clamp(g.height, heights.min, heights.max)};
    return length(point - geodetic_to_ecef(nearest));
}

bool below_horizon(Vec3d eye, const BoundingSphere& s) {
    // Earth as a sphere of the polar radius (the smallest, so nothing visible
    // is culled). Points X with dot(X, eye_dir) < R^2 / |eye| lie beyond the
    // horizon plane; the sphere is hidden if it lies wholly beyond it.
    const double r_earth = kWgs84B;
    const double d = length(eye);
    if (d <= r_earth) return false;
    const Vec3d dir = eye / d;
    return dot(s.center, dir) + s.radius < r_earth * r_earth / d;
}

namespace {

struct Walker {
    const TileIndex& index;
    const Camera& camera;
    const ReadyFn& ready;
    const SelectionOptions& options;
    Frustum frustum;
    Selection out;

    HeightRange expand(HeightRange h) const {
        const double margin = 0.1 * (h.max - h.min) + 50.0;
        return {h.min - margin, h.max + margin};
    }

    bool visible(const TileAddress& t, const HeightRange& h) {
        const BoundingSphere s = bounding_sphere(t, h);
        if (!frustum.intersects_sphere(s.center, s.radius) || below_horizon(camera.position, s)) {
            ++out.culled;
            return false;
        }
        return true;
    }

    void visit(const TileAddress& t, const HeightRange& inherited) {
        const std::optional<HeightRange> own = ready(t);
        const HeightRange heights = own ? expand(*own) : inherited;
        if (!visible(t, heights)) return;

        const bool drawable = index.has_data(t);
        const double sse = screen_error(t, distance_to_tile(camera.position, t, heights), camera);

        std::vector<TileAddress> kids;
        if (t.lod < options.max_lod) {
            for (const TileAddress& c : child_tiles(t)) {
                if (index.in_tree(c)) {
                    kids = child_tiles(t);  // refine into all of them; missing ones use ancestor data
                    break;
                }
            }
            // Past the finest data: finer meshes over the same data.
            if (kids.empty() && drawable && t.lod < index.finest_lod().value_or(t.lod) + options.extra_lods) {
                kids = child_tiles(t);
            }
        }
        if (!kids.empty() && (!drawable || sse > options.max_screen_error) && (own || !drawable)) {
            std::vector<TileAddress> shown;
            std::vector<TileRequest> pending;
            for (const TileAddress& c : kids) {
                const bool has_data = index.has_data(c);
                if (!has_data && !index.in_tree(c)) continue;  // nothing below it to show
                const std::optional<HeightRange> ch = ready(c);
                if (!visible(c, ch ? expand(*ch) : heights)) continue;
                shown.push_back(c);
                if (!ch && has_data) pending.push_back({c, sse});
            }
            // Refine once every visible child can be drawn; a tile with no
            // data of its own has nothing to show meanwhile, so it refines anyway.
            if (pending.empty() || !drawable) {
                if (own) out.refined.push_back(t);
                for (const TileAddress& c : shown) visit(c, heights);
                return;
            }
            out.requests.insert(out.requests.end(), pending.begin(), pending.end());
        }
        if (!drawable) return;
        if (own) {
            out.draw.push_back(t);
        } else {
            // Nothing coarser covers this tile on screen: load it first.
            out.requests.push_back({t, sse + 1.0e6});
        }
    }
};

}  // namespace

Selection select_tiles(const TileIndex& index, const Camera& camera, const ReadyFn& ready,
                       const SelectionOptions& options) {
    Walker w{index, camera, ready, options, Frustum::from_matrix(camera.projection() * camera.view()), {}};
    for (const TileAddress& root : index.roots()) w.visit(root, options.default_heights);
    std::sort(w.out.requests.begin(), w.out.requests.end(),
              [](const TileRequest& a, const TileRequest& b) { return a.priority > b.priority; });
    return std::move(w.out);
}

}  // namespace ogccdb::runtime
