# ogccdb-runtimepublisher

Engine-neutral streaming of OGC CDB 1.3 terrain for real-time applications. It reads a
CDB store through [ogccdb-core](https://github.com/fyrdm/ogccdb-core) and publishes, per
frame, the tiles a camera needs as ready-to-upload meshes and textures. Renderers and
game engines only upload and draw; LOD choice, paging, caching and loading stay here.

C++17 library (`ogccdb::runtime`) with a plain C API (`ogccdb/runtime/ogccdb_runtime.h`)
for engine plugins and other languages. MIT licence.

## What it does

- **Tile tree** (`tile_tree.hpp`): per geocell, one tile per negative LOD, then a quadtree
  from LOD 0 (CDB 1.3 Vol 1 §7.1.2.3). Indexes the Elevation and Imagery tiles a store
  holds; tiles missing at a LOD are filled from the finest ancestor that has data.
- **Selection** (`selection.hpp`): view-frustum and horizon culling, screen-space error
  refinement (one data texel vs. pixels), and no holes: a tile stays drawn until all its
  visible children are ready. Can refine a few LODs past the finest data for finer meshes.
- **Meshes and textures** (`terrain_mesh.hpp`): a regular grid per tile with skirts,
  normals, UVs and per-vertex heights; positions are float offsets from a double ECEF
  centre, so they stay precise at any zoom. Imagery is cropped to the tile.
- **Streaming** (`terrain.hpp`): worker threads read and build tiles by priority; decoded
  source tiles are shared and cached; memory budgets drop tiles not drawn lately.
- **Geodesy** (`geodesy.hpp`): WGS-84 geodetic <-> ECEF.

## Build

Needs an installed ogccdb-core 0.4 (`cmake --install` of ogccdb-core to a prefix).

```bash
cmake -B build -S . -G Ninja -DCMAKE_PREFIX_PATH=$HOME/ogccdb-prefix
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix $HOME/ogccdb-prefix
```

Then `find_package(ogccdb-runtimepublisher 0.1 CONFIG REQUIRED)` and link `ogccdb::runtime`.

## C++ use

```cpp
#include "ogccdb/runtime/terrain.hpp"
using namespace ogccdb::runtime;

Terrain terrain("/data/CDB");
// each frame:
Camera cam;  // ECEF position, direction, up, fov, viewport height, near/far
const std::vector<DrawTile>& tiles = terrain.update(cam);
for (const DrawTile& t : tiles) {
    // upload t.data->mesh and t.data->image once per t.tile, then draw at
    // t.data->mesh.center (relative to the camera, in double precision)
}
```

## C use

```c
#include "ogccdb/runtime/ogccdb_runtime.h"

ogccdb_rt_terrain* t = ogccdb_rt_open("/data/CDB", NULL);
if (!t) fprintf(stderr, "%s\n", ogccdb_rt_last_error());
int n = ogccdb_rt_update(t, &camera);
for (int i = 0; i < n; ++i) {
    ogccdb_rt_tile tile;
    ogccdb_rt_get_tile(t, i, &tile);  /* key: geocell_lat/lon, lod, u, r */
}
ogccdb_rt_close(t);
```

Engines with other axis conventions convert ECEF to their own frame (for instance a local
east-north-up frame at a chosen origin) in their adapter.

## Status

0.1: Elevation (primary terrain) and Imagery (one component). First consumer:
[ogccdb-viewer](https://github.com/fyrdm/ogccdb-viewer). Next: thin engine adapters on the
C API, Imagery components and Vstlm, tile-local origins for engine units.
