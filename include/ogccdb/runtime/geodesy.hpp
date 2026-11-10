#pragma once
// WGS-84 geodetic <-> earth-centred, earth-fixed (ECEF) conversions.
// CDB 1.3 uses WGS-84 latitude, longitude and ellipsoid heights.
#include "ogccdb/runtime/math.hpp"

namespace ogccdb::runtime {

/// WGS-84 semi-major axis and flattening, in metres.
inline constexpr double kWgs84A = 6378137.0;
inline constexpr double kWgs84F = 1.0 / 298.257223563;
inline constexpr double kWgs84B = kWgs84A * (1.0 - kWgs84F);
/// Mean earth radius, for rough metre-per-degree estimates.
inline constexpr double kMeanEarthRadius = 6371008.8;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegToRad = kPi / 180.0;
inline constexpr double kRadToDeg = 180.0 / kPi;

struct Geodetic {
    double lat = 0.0;     ///< degrees
    double lon = 0.0;     ///< degrees
    double height = 0.0;  ///< metres above the WGS-84 ellipsoid
};

/// ECEF position (metres) of a geodetic position.
Vec3d geodetic_to_ecef(const Geodetic& g);
/// Geodetic position of an ECEF point; accurate to well below a millimetre
/// for points near the earth's surface.
Geodetic ecef_to_geodetic(Vec3d p);
/// Unit ellipsoid normal ("up") at a latitude and longitude in degrees.
Vec3d ellipsoid_up(double lat, double lon);
/// Unit east and north vectors at a latitude and longitude in degrees.
Vec3d local_east(double lat, double lon);
Vec3d local_north(double lat, double lon);

}  // namespace ogccdb::runtime
