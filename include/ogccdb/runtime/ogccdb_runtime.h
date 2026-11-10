/* Plain C interface to the ogccdb runtime, for engines and languages that
 * cannot use the C++ API. Positions are ECEF metres (WGS-84); tile vertex
 * positions are relative to the tile's double-precision centre.
 *
 * Typical frame loop:
 *   n = ogccdb_rt_update(terrain, &camera);
 *   for i in 0..n-1: ogccdb_rt_get_tile(terrain, i, &tile);
 *       upload the tile once per (geocell, lod, u, r), then draw it.
 * Pointers in ogccdb_rt_tile stay valid until the next ogccdb_rt_update(). */
#ifndef OGCCDB_RUNTIME_H
#define OGCCDB_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ogccdb_rt_terrain ogccdb_rt_terrain;

typedef struct ogccdb_rt_options {
    int mesh_resolution;        /* grid cells per tile side */
    double max_screen_error;    /* pixels per data texel before refining */
    int max_lod;                /* finest LOD to refine to */
    int extra_lods;             /* LODs to refine past the finest data (finer meshes) */
    int worker_threads;         /* 0: from the CPU count */
    uint64_t tile_budget_bytes;
    uint64_t source_budget_bytes;
    int imagery_selector1;      /* imagery component (CS1, CS2) */
    int imagery_selector2;
} ogccdb_rt_options;

typedef struct ogccdb_rt_camera {
    double position[3];
    double direction[3];
    double up[3];
    double fov_y;        /* radians */
    double aspect;       /* width / height */
    int viewport_height; /* pixels */
    double near_plane;
    double far_plane;
} ogccdb_rt_camera;

typedef struct ogccdb_rt_tile {
    int geocell_lat, geocell_lon, lod, u, r;
    double center[3];
    float up[3];
    float min_height, max_height;
    int vertex_count;
    const float* positions; /* 3 per vertex */
    const float* normals;   /* 3 per vertex */
    const float* uvs;       /* 2 per vertex, (0, 0) at the north-west corner */
    const float* heights;   /* 1 per vertex */
    int index_count;
    const uint32_t* indices;
    int image_width, image_height; /* 0 if the tile has no imagery */
    const uint8_t* image_rgb;      /* rows from the north, 3 bytes per pixel */
} ogccdb_rt_tile;

typedef struct ogccdb_rt_stats {
    uint64_t frame;
    uint64_t drawn, queued, loading, resident;
    uint64_t tile_bytes, source_bytes;
    uint64_t loaded_total, failed_total;
} ogccdb_rt_stats;

/* Fills `o` with the defaults. */
void ogccdb_rt_default_options(ogccdb_rt_options* o);

/* Opens a CDB 1.3 store. `options` may be NULL. Returns NULL on error; see
 * ogccdb_rt_last_error(). */
ogccdb_rt_terrain* ogccdb_rt_open(const char* cdb_root, const ogccdb_rt_options* options);
void ogccdb_rt_close(ogccdb_rt_terrain* t);

/* Message of the last error on this thread; empty if none. */
const char* ogccdb_rt_last_error(void);

/* Selects and queues tiles for `camera`; returns the number of tiles to draw,
 * or -1 on error. */
int ogccdb_rt_update(ogccdb_rt_terrain* t, const ogccdb_rt_camera* camera);
/* Tile `i` of the last update; returns 0, or -1 if `i` is out of range. */
int ogccdb_rt_get_tile(const ogccdb_rt_terrain* t, int i, ogccdb_rt_tile* out);
void ogccdb_rt_get_stats(const ogccdb_rt_terrain* t, ogccdb_rt_stats* out);
/* Blocks until no tile is queued or loading. */
void ogccdb_rt_wait_idle(const ogccdb_rt_terrain* t);

/* Extent of the store's geocells in degrees; returns 0, or -1 if empty. */
int ogccdb_rt_extent(const ogccdb_rt_terrain* t, double* south, double* west, double* north, double* east);

void ogccdb_rt_geodetic_to_ecef(double lat_deg, double lon_deg, double height, double out[3]);
void ogccdb_rt_ecef_to_geodetic(const double in[3], double* lat_deg, double* lon_deg, double* height);

#ifdef __cplusplus
}
#endif

#endif /* OGCCDB_RUNTIME_H */
