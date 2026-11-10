# Decisions

- 2026-11-10 — The runtime is engine-neutral C++17 with a plain C API, so engine adapters
  (in their own languages) and the viewer share one implementation of LOD selection,
  paging, caching and async loading. Meshes are ECEF float offsets from a double centre;
  adapters convert to their engine's frame.
- 2026-11-10 — Render tiles follow the CDB tile tree. A tile refines only into all its
  visible children at once, and a child with no tile of its own is drawn from its finest
  ancestor's data. CDB 1.3 says each tile's parent exists (Vol 1 §7.1.2.2 Requirement 15)
  but does not ask for all four siblings, so partial coverage at a LOD is expected.
- 2026-11-10 — Refinement metric: the screen size of one data-grid spacing of the tile's
  LOD (raster_size(lod) elements per geocell side, Vol 1 §7.1.2). The spec is silent on
  rendering, so this is our choice. Default threshold 1.5 px.
- 2026-11-10 — Elevation posts are corner-aligned (Vol 1 §10.6.1): the strip past the last
  post on the east and south edges belongs to the neighbour tile. Meshes take the nearest
  post there and hide the small step with skirts.
- 2026-11-10 — `children` of a tile is implemented here (inverse of
  `ogccdb::cdb13::parent_tile`, Vol 1 §7.1.2.3). It is pure addressing and should move to
  ogccdb-core in its next minor release.
