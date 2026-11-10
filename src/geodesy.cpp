#include "ogccdb/runtime/geodesy.hpp"

#include <cmath>

namespace ogccdb::runtime {
namespace {
constexpr double kE2 = kWgs84F * (2.0 - kWgs84F);  // first eccentricity squared
}

Vec3d geodetic_to_ecef(const Geodetic& g) {
    const double lat = g.lat * kDegToRad, lon = g.lon * kDegToRad;
    const double s = std::sin(lat), c = std::cos(lat);
    const double n = kWgs84A / std::sqrt(1.0 - kE2 * s * s);
    return {(n + g.height) * c * std::cos(lon), (n + g.height) * c * std::sin(lon),
            (n * (1.0 - kE2) + g.height) * s};
}

Geodetic ecef_to_geodetic(Vec3d p) {
    const double lon = std::atan2(p.y, p.x);
    const double r = std::hypot(p.x, p.y);
    if (r < 1e-9) {  // on the polar axis
        const double lat = p.z >= 0.0 ? 90.0 : -90.0;
        return {lat, 0.0, std::fabs(p.z) - kWgs84B};
    }
    // Fixed-point iteration on latitude; converges in a few steps near the surface.
    double lat = std::atan2(p.z, r * (1.0 - kE2));
    double h = 0.0;
    for (int i = 0; i < 6; ++i) {
        const double s = std::sin(lat);
        const double n = kWgs84A / std::sqrt(1.0 - kE2 * s * s);
        h = r / std::cos(lat) - n;
        lat = std::atan2(p.z, r * (1.0 - kE2 * n / (n + h)));
    }
    return {lat * kRadToDeg, lon * kRadToDeg, h};
}

Vec3d ellipsoid_up(double lat, double lon) {
    const double la = lat * kDegToRad, lo = lon * kDegToRad;
    return {std::cos(la) * std::cos(lo), std::cos(la) * std::sin(lo), std::sin(la)};
}

Vec3d local_east(double /*lat*/, double lon) {
    const double lo = lon * kDegToRad;
    return {-std::sin(lo), std::cos(lo), 0.0};
}

Vec3d local_north(double lat, double lon) {
    const double la = lat * kDegToRad, lo = lon * kDegToRad;
    return {-std::sin(la) * std::cos(lo), -std::sin(la) * std::sin(lo), std::cos(la)};
}

}  // namespace ogccdb::runtime
