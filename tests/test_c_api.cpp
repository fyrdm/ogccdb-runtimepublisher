#include <gtest/gtest.h>

#include "ogccdb/runtime/ogccdb_runtime.h"

extern "C" int ogccdb_rt_c_smoke(void);

TEST(CApi, CompilesAsCAndWorks) { EXPECT_EQ(ogccdb_rt_c_smoke(), 0); }

TEST(CApi, OpenMissingStoreFails) {
    EXPECT_EQ(ogccdb_rt_open("/nonexistent/cdb", nullptr), nullptr);
    EXPECT_NE(std::string(ogccdb_rt_last_error()), "");
}
