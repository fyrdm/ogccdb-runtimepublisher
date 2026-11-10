#include <gtest/gtest.h>

#include "ogccdb/cdb13/lod.hpp"
#include "ogccdb/runtime/tile_tree.hpp"

using namespace ogccdb;
using namespace ogccdb::runtime;

namespace {
const Geocell kCell{47, 8};
}

TEST(TileTree, ChildrenAreInverseOfParent) {
    for (int lod = cdb13::kMinLod; lod < 4; ++lod) {
        const int n = cdb13::tiles_per_side(lod);
        for (int u = 0; u < n; ++u) {
            for (int r = 0; r < n; ++r) {
                const TileAddress t{kCell, lod, u, r};
                const auto kids = child_tiles(t);
                EXPECT_EQ(kids.size(), lod < 0 ? 1u : 4u);
                for (const TileAddress& c : kids) {
                    EXPECT_EQ(cdb13::parent_tile(c), t);
                    EXPECT_TRUE(covers(t, c));
                }
            }
        }
    }
    EXPECT_TRUE(child_tiles({kCell, cdb13::kMaxLod, 0, 0}).empty());
    EXPECT_THROW(child_tiles({kCell, 0, 1, 0}), std::invalid_argument);
}

TEST(TileTree, Covers) {
    EXPECT_TRUE(covers({kCell, -10, 0, 0}, {kCell, 5, 17, 3}));
    EXPECT_TRUE(covers({kCell, 2, 1, 3}, {kCell, 4, 6, 13}));
    EXPECT_FALSE(covers({kCell, 2, 1, 3}, {kCell, 4, 6, 11}));
    EXPECT_FALSE(covers({kCell, 4, 6, 13}, {kCell, 2, 1, 3}));
    EXPECT_FALSE(covers({{47, 9}, -10, 0, 0}, {kCell, 0, 0, 0}));
}

TEST(TileIndex, SourcesAndTree) {
    TileIndex idx;
    EXPECT_FALSE(idx.extent());
    idx.add(cdb13::kElevation, {kCell, 0, 0, 0});
    idx.add(cdb13::kImagery, {kCell, 2, 3, 1});
    idx.add(99, {kCell, 3, 0, 0});  // other datasets are ignored

    ASSERT_EQ(idx.roots().size(), 1u);
    EXPECT_EQ(idx.roots()[0], (TileAddress{kCell, -10, 0, 0}));
    EXPECT_TRUE(idx.in_tree({kCell, -5, 0, 0}));
    EXPECT_TRUE(idx.in_tree({kCell, 1, 1, 0}));
    EXPECT_FALSE(idx.in_tree({kCell, 1, 0, 0}));
    EXPECT_FALSE(idx.in_tree({kCell, 3, 0, 0}));
    EXPECT_EQ(idx.finest_lod(), 2);

    EXPECT_FALSE(idx.has_data({kCell, -1, 0, 0}));
    EXPECT_EQ(idx.elevation_source({kCell, 4, 9, 9}), (TileAddress{kCell, 0, 0, 0}));
    EXPECT_EQ(idx.imagery_source({kCell, 3, 6, 2}), (TileAddress{kCell, 2, 3, 1}));
    EXPECT_FALSE(idx.imagery_source({kCell, 3, 0, 0}));

    const auto e = idx.extent();
    ASSERT_TRUE(e);
    EXPECT_DOUBLE_EQ(e->south, 47.0);
    EXPECT_DOUBLE_EQ(e->east, 9.0);
}
