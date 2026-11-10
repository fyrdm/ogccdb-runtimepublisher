#include "ogccdb/runtime/ogccdb_runtime.h"

#include <exception>
#include <string>

#include "ogccdb/runtime/geodesy.hpp"
#include "ogccdb/runtime/terrain.hpp"

using namespace ogccdb::runtime;

struct ogccdb_rt_terrain {
    std::unique_ptr<Terrain> terrain;
    const std::vector<DrawTile>* draw = nullptr;
};

namespace {
thread_local std::string g_last_error;

void set_error(const char* what) { g_last_error = what ? what : "unknown error"; }

Vec3d vec(const double v[3]) { return {v[0], v[1], v[2]}; }
}  // namespace

extern "C" {

void ogccdb_rt_default_options(ogccdb_rt_options* o) {
    if (!o) return;
    const TerrainOptions d;
    o->mesh_resolution = d.mesh.resolution;
    o->max_screen_error = d.selection.max_screen_error;
    o->max_lod = d.selection.max_lod;
    o->extra_lods = d.selection.extra_lods;
    o->worker_threads = d.worker_threads;
    o->tile_budget_bytes = d.tile_budget_bytes;
    o->source_budget_bytes = d.source_budget_bytes;
    o->imagery_selector1 = d.imagery.selector1;
    o->imagery_selector2 = d.imagery.selector2;
}

ogccdb_rt_terrain* ogccdb_rt_open(const char* cdb_root, const ogccdb_rt_options* options) {
    g_last_error.clear();
    if (!cdb_root) {
        set_error("ogccdb_rt_open: cdb_root is NULL");
        return nullptr;
    }
    try {
        TerrainOptions o;
        if (options) {
            o.mesh.resolution = options->mesh_resolution;
            o.selection.max_screen_error = options->max_screen_error;
            o.selection.max_lod = options->max_lod;
            o.selection.extra_lods = options->extra_lods;
            o.worker_threads = options->worker_threads;
            o.tile_budget_bytes = static_cast<std::size_t>(options->tile_budget_bytes);
            o.source_budget_bytes = static_cast<std::size_t>(options->source_budget_bytes);
            o.imagery = {options->imagery_selector1, options->imagery_selector2};
        }
        auto* t = new ogccdb_rt_terrain;
        t->terrain = std::make_unique<Terrain>(cdb_root, o);
        return t;
    } catch (const std::exception& e) {
        set_error(e.what());
    } catch (...) {
        set_error(nullptr);
    }
    return nullptr;
}

void ogccdb_rt_close(ogccdb_rt_terrain* t) { delete t; }

const char* ogccdb_rt_last_error(void) { return g_last_error.c_str(); }

int ogccdb_rt_update(ogccdb_rt_terrain* t, const ogccdb_rt_camera* c) {
    if (!t || !c) {
        set_error("ogccdb_rt_update: NULL argument");
        return -1;
    }
    try {
        Camera cam;
        cam.position = vec(c->position);
        cam.direction = vec(c->direction);
        cam.up = vec(c->up);
        cam.fov_y = c->fov_y;
        cam.aspect = c->aspect;
        cam.viewport_height = c->viewport_height;
        cam.near_plane = c->near_plane;
        cam.far_plane = c->far_plane;
        t->draw = &t->terrain->update(cam);
        return static_cast<int>(t->draw->size());
    } catch (const std::exception& e) {
        set_error(e.what());
    } catch (...) {
        set_error(nullptr);
    }
    return -1;
}

int ogccdb_rt_get_tile(const ogccdb_rt_terrain* t, int i, ogccdb_rt_tile* out) {
    if (!t || !out || !t->draw || i < 0 || static_cast<std::size_t>(i) >= t->draw->size()) return -1;
    const DrawTile& d = (*t->draw)[static_cast<std::size_t>(i)];
    const TerrainMesh& m = d.data->mesh;
    out->geocell_lat = d.tile.geocell.lat;
    out->geocell_lon = d.tile.geocell.lon;
    out->lod = d.tile.lod;
    out->u = d.tile.u;
    out->r = d.tile.r;
    out->center[0] = m.center.x;
    out->center[1] = m.center.y;
    out->center[2] = m.center.z;
    out->up[0] = static_cast<float>(m.up.x);
    out->up[1] = static_cast<float>(m.up.y);
    out->up[2] = static_cast<float>(m.up.z);
    out->min_height = m.min_height;
    out->max_height = m.max_height;
    out->vertex_count = static_cast<int>(m.vertex_count());
    out->positions = m.positions.data();
    out->normals = m.normals.data();
    out->uvs = m.uvs.data();
    out->heights = m.heights.data();
    out->index_count = static_cast<int>(m.indices.size());
    out->indices = m.indices.data();
    const Image& img = d.data->image;
    out->image_width = img.width;
    out->image_height = img.height;
    out->image_rgb = img.empty() ? nullptr : img.rgb.data();
    return 0;
}

void ogccdb_rt_get_stats(const ogccdb_rt_terrain* t, ogccdb_rt_stats* out) {
    if (!t || !out) return;
    const TerrainStats s = t->terrain->stats();
    out->frame = s.frame;
    out->drawn = s.drawn;
    out->queued = s.queued;
    out->loading = s.loading;
    out->resident = s.resident;
    out->tile_bytes = s.tile_bytes;
    out->source_bytes = s.source_bytes;
    out->loaded_total = s.loaded_total;
    out->failed_total = s.failed_total;
}

void ogccdb_rt_wait_idle(const ogccdb_rt_terrain* t) {
    if (t) t->terrain->wait_idle();
}

int ogccdb_rt_extent(const ogccdb_rt_terrain* t, double* south, double* west, double* north, double* east) {
    if (!t) return -1;
    const auto e = t->terrain->index().extent();
    if (!e) return -1;
    if (south) *south = e->south;
    if (west) *west = e->west;
    if (north) *north = e->north;
    if (east) *east = e->east;
    return 0;
}

void ogccdb_rt_geodetic_to_ecef(double lat_deg, double lon_deg, double height, double out[3]) {
    const Vec3d p = geodetic_to_ecef({lat_deg, lon_deg, height});
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void ogccdb_rt_ecef_to_geodetic(const double in[3], double* lat_deg, double* lon_deg, double* height) {
    const Geodetic g = ecef_to_geodetic(vec(in));
    if (lat_deg) *lat_deg = g.lat;
    if (lon_deg) *lon_deg = g.lon;
    if (height) *height = g.height;
}

}  // extern "C"
