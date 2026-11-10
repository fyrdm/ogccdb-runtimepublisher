#include <gtest/gtest.h>

#include "ogccdb/runtime/geodesy.hpp"

using namespace ogccdb::runtime;

TEST(Geodesy, EquatorAndPole) {
    const Vec3d p = geodetic_to_ecef({0.0, 0.0, 0.0});
    EXPECT_NEAR(p.x, kWgs84A, 1e-6);
    EXPECT_NEAR(p.y, 0.0, 1e-6);
    const Vec3d n = geodetic_to_ecef({90.0, 0.0, 100.0});
    EXPECT_NEAR(n.z, kWgs84B + 100.0, 1e-6);
}

TEST(Geodesy, RoundTrip) {
    for (double lat : {-89.5, -47.3, 0.0, 12.25, 47.5, 89.9}) {
        for (double lon : {-179.0, -8.5, 0.0, 8.5, 179.9}) {
            for (double h : {-400.0, 0.0, 2800.0, 400000.0}) {
                const Geodetic g = ecef_to_geodetic(geodetic_to_ecef({lat, lon, h}));
                EXPECT_NEAR(g.lat, lat, 1e-9);
                EXPECT_NEAR(g.lon, lon, 1e-9);
                EXPECT_NEAR(g.height, h, 1e-4);
            }
        }
    }
}

TEST(Geodesy, LocalFrameIsOrthonormal) {
    const Vec3d u = ellipsoid_up(47.5, 8.5), e = local_east(47.5, 8.5), n = local_north(47.5, 8.5);
    EXPECT_NEAR(length(u), 1.0, 1e-12);
    EXPECT_NEAR(dot(u, e), 0.0, 1e-12);
    EXPECT_NEAR(dot(u, n), 0.0, 1e-12);
    const Vec3d c = cross(e, n);
    EXPECT_NEAR(dot(c, u), 1.0, 1e-12);
}

TEST(Math, FrustumCullsBehindCamera) {
    const Mat4d vp = perspective(1.0, 1.0, 1.0, 1000.0) * look_at_direction({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    const Frustum f = Frustum::from_matrix(vp);
    EXPECT_TRUE(f.intersects_sphere({0, 0, -10}, 1.0));
    EXPECT_FALSE(f.intersects_sphere({0, 0, 10}, 1.0));
    EXPECT_FALSE(f.intersects_sphere({0, 0, -2000}, 1.0));
    EXPECT_TRUE(f.intersects_sphere({0, 0, 5}, 6.0));
}
