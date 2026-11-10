#pragma once
// Small double-precision vector and matrix types for camera and culling math.
// Matrices are column-major (m[col * 4 + row]) and map column vectors, as in OpenGL.
#include <array>
#include <cmath>

namespace ogccdb::runtime {

struct Vec3d {
    double x = 0.0, y = 0.0, z = 0.0;

    friend Vec3d operator+(Vec3d a, Vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3d operator-(Vec3d a, Vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3d operator-(Vec3d a) { return {-a.x, -a.y, -a.z}; }
    friend Vec3d operator*(Vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
    friend Vec3d operator*(double s, Vec3d a) { return a * s; }
    friend Vec3d operator/(Vec3d a, double s) { return {a.x / s, a.y / s, a.z / s}; }
    Vec3d& operator+=(Vec3d b) { return *this = *this + b; }
    Vec3d& operator-=(Vec3d b) { return *this = *this - b; }
};

inline double dot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3d cross(Vec3d a, Vec3d b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(Vec3d a) { return std::sqrt(dot(a, a)); }
/// `a` scaled to unit length; `a` itself if it has zero length.
inline Vec3d normalize(Vec3d a) {
    const double l = length(a);
    return l > 0.0 ? a / l : a;
}

struct Vec4d {
    double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
};

/// 4x4 matrix, column-major.
struct Mat4d {
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    double& operator()(int row, int col) { return m[static_cast<std::size_t>(col * 4 + row)]; }
    double operator()(int row, int col) const { return m[static_cast<std::size_t>(col * 4 + row)]; }
};

Mat4d operator*(const Mat4d& a, const Mat4d& b);
Vec4d operator*(const Mat4d& a, const Vec4d& v);

/// View matrix of a camera at `eye` looking along `direction` with `up` roughly up.
Mat4d look_at_direction(Vec3d eye, Vec3d direction, Vec3d up);
/// Perspective projection to OpenGL clip space (z in -1..1). `fov_y` in radians.
Mat4d perspective(double fov_y, double aspect, double near_plane, double far_plane);

/// Plane a*x + b*y + c*z + d = 0 with unit normal (a, b, c) pointing inside.
struct Plane {
    Vec3d normal;
    double d = 0.0;
    double distance(Vec3d p) const { return dot(normal, p) + d; }
};

/// The six planes of a view frustum (left, right, bottom, top, near, far),
/// taken from a projection * view matrix.
struct Frustum {
    std::array<Plane, 6> planes;

    static Frustum from_matrix(const Mat4d& view_projection);
    /// False only if the sphere lies wholly outside one plane.
    bool intersects_sphere(Vec3d center, double radius) const;
};

}  // namespace ogccdb::runtime
