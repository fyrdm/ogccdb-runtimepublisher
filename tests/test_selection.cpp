#include <gtest/gtest.h>

#include <algorithm>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/runtime/geodesy.hpp"
#include "ogccdb/runtime/selection.hpp"

using namespace ogccdb;
using namespace ogccdb::runtime;

namespace {
const Geocell kCell{47, 8};

// Every tile of the geocell from LOD -10 to `finest`, as both datasets.
TileIndex full_index(int finest) {
    TileIndex idx;
    for (int lod = cdb13::kMinLod; lod <= finest; ++lod) {
        const int n = cdb13::tiles_per_side(lod);
        for (int u = 0; u < n; ++u) {
            for (int r = 0; r < n; ++r) {
                idx.add(cdb13::kElevation, {kCell, lod, u, r});
                idx.add(cdb13::kImagery, {kCell, lod, u, r});
            }
        }
    }
    return idx;
}

Camera looking_down(double lat, double lon, double height) {
    Camera c;
    c.position = geodetic_to_ecef({lat, lon, height});
    c.direction = -ellipsoid_up(lat, lon);
    c.up = local_north(lat, lon);
    c.aspect = 1.0;
    c.viewport_height = 1000;
    c.near_plane = 1.0;
    c.far_plane = 2.0e7;
    return c;
}

ReadyFn all_ready() {
    return [](const TileAddress&) { return std::optional<HeightRange>(HeightRange{300, 2800}); };
}

int finest(const Selection& s) {
    int f = cdb13::kMinLod;
    for (const TileAddress& t : s.draw) f = std::max(f, t.lod);
    return f;
}
}  // namespace

SelectionOptions no_extra() {
    SelectionOptions o;
    o.extra_lods = 0;
    return o;
}

TEST(Selection, NothingReadyRequestsTheRoot) {
    const TileIndex idx = full_index(2);
    const Selection s =
        select_tiles(idx, looking_down(47.5, 8.5, 5000), [](const TileAddress&) { return std::nullopt; });
    EXPECT_TRUE(s.draw.empty());
    ASSERT_EQ(s.requests.size(), 1u);
    EXPECT_EQ(s.requests[0].tile, idx.roots()[0]);
}

TEST(Selection, FarIsCoarseNearIsFine) {
    const TileIndex idx = full_index(3);
    const Selection far = select_tiles(idx, looking_down(47.5, 8.5, 5.0e6), all_ready());
    const Selection near = select_tiles(idx, looking_down(47.5, 8.5, 3000), all_ready(), no_extra());
    ASSERT_FALSE(far.draw.empty());
    ASSERT_FALSE(near.draw.empty());
    EXPECT_LT(finest(far), 0);
    EXPECT_EQ(finest(near), 3);
    EXPECT_TRUE(near.requests.empty());
    // With extra LODs, close views refine past the data.
    SelectionOptions extra;
    extra.extra_lods = 2;
    EXPECT_EQ(finest(select_tiles(idx, looking_down(47.5, 8.5, 1000), all_ready(), extra)), 5);
}

TEST(Selection, ReportsRefinedAncestors) {
    const TileIndex idx = full_index(2);
    const Selection s = select_tiles(idx, looking_down(47.5, 8.5, 3000), all_ready(), no_extra());
    // The chain LOD -10..1 above the drawn LOD 2 tiles: 11 tiles + 4 at LOD 1.
    ASSERT_FALSE(s.refined.empty());
    for (const TileAddress& t : s.draw) {
        for (std::optional<TileAddress> a = cdb13::parent_tile(t); a; a = cdb13::parent_tile(*a)) {
            EXPECT_NE(std::find(s.refined.begin(), s.refined.end(), *a), s.refined.end());
        }
    }
}

TEST(Selection, DrawnTilesDoNotOverlap) {
    const TileIndex idx = full_index(3);
    const Selection s = select_tiles(idx, looking_down(47.2, 8.2, 20000), all_ready());
    for (std::size_t a = 0; a < s.draw.size(); ++a) {
        for (std::size_t b = 0; b < s.draw.size(); ++b) {
            if (a != b) {
                EXPECT_FALSE(covers(s.draw[a], s.draw[b]));
            }
        }
    }
}

TEST(Selection, KeepsParentUntilChildrenReady) {
    const TileIndex idx = full_index(1);
    const TileAddress lod0{kCell, 0, 0, 0};
    // Ready down to LOD 0 only.
    const ReadyFn ready = [&](const TileAddress& t) {
        return t.lod <= 0 ? std::optional<HeightRange>(HeightRange{300, 2800}) : std::nullopt;
    };
    const Selection s = select_tiles(idx, looking_down(47.5, 8.5, 3000), ready, no_extra());
    ASSERT_EQ(s.draw.size(), 1u);
    EXPECT_EQ(s.draw[0], lod0);
    EXPECT_EQ(s.requests.size(), 4u);
    for (const TileRequest& q : s.requests) EXPECT_EQ(q.tile.lod, 1);
}

TEST(Selection, PartialChildrenAreFilledFromAncestors) {
    TileIndex idx = full_index(0);
    idx.add(cdb13::kImagery, {kCell, 1, 1, 1});  // only the north-east quarter at LOD 1
    const Selection s = select_tiles(idx, looking_down(47.5, 8.5, 3000), all_ready(), no_extra());
    std::size_t lod1 = 0;
    for (const TileAddress& t : s.draw) lod1 += t.lod == 1;
    EXPECT_EQ(lod1, 4u);  // the three missing quarters come from LOD 0
}

TEST(Selection, CullsTheOtherSideOfTheEarth) {
    const TileIndex idx = full_index(0);
    Camera c = looking_down(-47.5, -171.5, 3000);  // antipode
    c.direction = ellipsoid_up(-47.5, -171.5) * -1.0;
    const Selection s = select_tiles(idx, c, all_ready());
    EXPECT_TRUE(s.draw.empty());
    EXPECT_TRUE(s.requests.empty());
}
