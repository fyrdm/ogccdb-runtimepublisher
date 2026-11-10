#include <gtest/gtest.h>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/runtime/geodesy.hpp"
#include "ogccdb/runtime/terrain_mesh.hpp"
#include "ogccdb/runtime/tile_tree.hpp"

using namespace ogccdb;
using namespace ogccdb::runtime;

namespace {
const Geocell kCell{47, 8};

cdb13::ElevationTile ramp_elevation(TileAddress t) {
    const int n = cdb13::raster_size(t.lod);
    cdb13::ElevationTile e{t, {}};
    e.elevation.resize(static_cast<std::size_t>(n) * n);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) e.elevation[static_cast<std::size_t>(j) * n + i] = static_cast<float>(100 * i);
    }
    return e;
}
}  // namespace

TEST(TerrainMesh, SampleHeightFollowsCornerPosts) {
    const auto e = ramp_elevation({kCell, -6, 0, 0});  // 16 x 16 posts over 1 degree
    EXPECT_NEAR(sample_height(e, 47.5, 8.0), 0.0, 1e-6);
    EXPECT_NEAR(sample_height(e, 47.5, 8.0 + 3.0 / 16), 300.0, 1e-3);
    EXPECT_NEAR(sample_height(e, 47.5, 8.0 + 3.5 / 16), 350.0, 1e-3);
    EXPECT_NEAR(sample_height(e, 47.5, 9.0), 1500.0, 1e-3);  // past the last post: clamped
}

TEST(TerrainMesh, GridAndSkirt) {
    const TileAddress t{kCell, -6, 0, 0};
    const auto e = ramp_elevation(t);
    MeshOptions o;
    o.resolution = 8;
    const TerrainMesh m = build_terrain_mesh(t, &e, o);
    EXPECT_EQ(m.vertex_count(), 9u * 9u + 4u * 8u);
    EXPECT_EQ(m.indices.size(), 8u * 8u * 6u + 4u * 8u * 6u);
    EXPECT_EQ(m.positions.size(), m.vertex_count() * 3);
    EXPECT_EQ(m.uvs.size(), m.vertex_count() * 2);
    EXPECT_FLOAT_EQ(m.min_height, 0.0f);
    EXPECT_NEAR(m.max_height, 1500.0f, 1e-2);
    for (std::uint32_t i : m.indices) EXPECT_LT(i, m.vertex_count());

    // Vertex 0 is the north-west corner at height 0.
    const Vec3d nw = m.center + Vec3d{m.positions[0], m.positions[1], m.positions[2]};
    const Geodetic g = ecef_to_geodetic(nw);
    EXPECT_NEAR(g.lat, 48.0, 1e-6);
    EXPECT_NEAR(g.lon, 8.0, 1e-6);
    EXPECT_NEAR(g.height, 0.0, 0.05);
    // Normals point up-ish and lean west where the ground rises to the east.
    const Vec3d n{m.normals[3 * 40], m.normals[3 * 40 + 1], m.normals[3 * 40 + 2]};
    EXPECT_GT(dot(n, m.up), 0.9);
    EXPECT_LT(dot(n, local_east(47.5, 8.5)), 0.0);
}

TEST(TerrainMesh, ChildTileUsesAncestorElevation) {
    const auto e = ramp_elevation({kCell, -6, 0, 0});
    MeshOptions o;
    o.resolution = 4;
    const TerrainMesh m = build_terrain_mesh({kCell, 1, 0, 1}, &e, o);  // south-east quarter
    EXPECT_NEAR(m.min_height, 800.0f, 1e-2);
    EXPECT_NEAR(m.max_height, 1500.0f, 1e-2);
    EXPECT_THROW(build_terrain_mesh({{47, 9}, 1, 0, 0}, &e, o), std::invalid_argument);
    const TerrainMesh flat = build_terrain_mesh({kCell, 1, 0, 1}, nullptr, o);
    EXPECT_FLOAT_EQ(flat.max_height, 0.0f);
}

TEST(TerrainMesh, CropImagery) {
    const TileAddress src{kCell, -7, 0, 0};  // 8 x 8 pixels
    cdb13::ImageryTile img{src, cdb13::kYearlyVsti, 1, {}};
    for (int j = 0; j < 8; ++j) {
        for (int i = 0; i < 8; ++i) img.pixels.push_back(static_cast<std::uint8_t>(10 * j + i));
    }
    const Image whole = crop_imagery(src, img);
    EXPECT_EQ(whole.width, 8);
    EXPECT_EQ(whole.rgb.size(), 8u * 8u * 3u);
    // LOD 1, U 0 (south), R 1 (east): rows 4..7, columns 4..7.
    const Image q = crop_imagery({kCell, 1, 0, 1}, img);
    ASSERT_EQ(q.width, 4);
    ASSERT_EQ(q.height, 4);
    EXPECT_EQ(q.rgb[0], 44);
    EXPECT_EQ(q.rgb[1], 44);
    EXPECT_EQ(q.rgb.back(), 77);
    // Finer than the source has pixels: at least one pixel.
    const Image tiny = crop_imagery({kCell, 5, 0, 0}, img);
    EXPECT_EQ(tiny.width, 1);
    EXPECT_EQ(tiny.rgb[0], 70);
}
