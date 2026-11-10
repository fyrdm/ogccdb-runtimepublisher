#include <gtest/gtest.h>

#include <filesystem>
#include <random>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/cdb13/store.hpp"
#include "ogccdb/runtime/geodesy.hpp"
#include "ogccdb/runtime/terrain.hpp"

namespace fs = std::filesystem;
using namespace ogccdb;
using namespace ogccdb::runtime;

namespace {
const Geocell kCell{47, 8};

// A small store: Elevation and Imagery from LOD -10 to -5 (at most 32 x 32).
fs::path make_store() {
    std::mt19937 rng(42);
    const fs::path root = fs::temp_directory_path() / ("ogccdb_runtime_test_" + std::to_string(rng()));
    fs::remove_all(root);
    cdb13::Store store = cdb13::Store::create(root);
    for (int lod = cdb13::kMinLod; lod <= -5; ++lod) {
        const int n = cdb13::raster_size(lod);
        cdb13::ElevationTile e{{kCell, lod, 0, 0}, std::vector<float>(static_cast<std::size_t>(n) * n, 500.0f)};
        store.write_elevation(e);
        cdb13::ImageryTile img{{kCell, lod, 0, 0}, cdb13::kYearlyVsti, 3,
                               std::vector<std::uint8_t>(static_cast<std::size_t>(n) * n * 3, 120)};
        store.write_imagery(img);
    }
    return root;
}

Camera above(double height) {
    Camera c;
    c.position = geodetic_to_ecef({47.5, 8.5, height});
    c.direction = -ellipsoid_up(47.5, 8.5);
    c.up = local_north(47.5, 8.5);
    c.aspect = 1.0;
    c.viewport_height = 800;
    c.near_plane = 1.0;
    c.far_plane = 2.0e7;
    return c;
}
}  // namespace

TEST(Terrain, StreamsDownToTheFinestLod) {
    const fs::path root = make_store();
    {
        TerrainOptions o;
        o.mesh.resolution = 8;
        o.worker_threads = 2;
        o.selection.extra_lods = 0;
        Terrain terrain(root, o);
        EXPECT_EQ(terrain.index().elevation_count(), 6u);
        EXPECT_EQ(terrain.index().imagery_count(), 6u);

        const Camera cam = above(2000);
        // Each round refines at most one LOD.
        int finest = cdb13::kMinLod - 1;
        for (int i = 0; i < 40; ++i) {
            const auto& draw = terrain.update(cam);
            terrain.wait_idle();
            for (const DrawTile& d : draw) finest = std::max(finest, d.tile.lod);
        }
        const auto& draw = terrain.update(cam);
        ASSERT_FALSE(draw.empty());
        // The store ends at LOD -5, so nothing finer is drawn.
        EXPECT_EQ(finest, -5);
        EXPECT_EQ(draw[0].tile.lod, -5);
        const TerrainTile& t = *draw[0].data;
        EXPECT_EQ(t.elevation_source, (TileAddress{kCell, -5, 0, 0}));
        EXPECT_EQ(t.image.width, 32);
        EXPECT_NEAR(t.mesh.min_height, 500.0f, 1e-3);
        const TerrainStats s = terrain.stats();
        EXPECT_EQ(s.failed_total, 0u);
        EXPECT_GE(s.loaded_total, 6u);
        EXPECT_EQ(s.drawn, 1u);
    }
    fs::remove_all(root);
}

TEST(Terrain, RejectsAFolderThatIsNotACdb) {
    EXPECT_THROW(Terrain(fs::temp_directory_path() / "ogccdb_runtime_missing"), std::runtime_error);
}
