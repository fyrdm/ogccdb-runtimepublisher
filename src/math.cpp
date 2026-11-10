#include "ogccdb/runtime/math.hpp"

namespace ogccdb::runtime {

Mat4d operator*(const Mat4d& a, const Mat4d& b) {
    Mat4d r;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a(row, k) * b(k, col);
            r(row, col) = s;
        }
    }
    return r;
}

Vec4d operator*(const Mat4d& a, const Vec4d& v) {
    const double in[4] = {v.x, v.y, v.z, v.w};
    double out[4];
    for (int row = 0; row < 4; ++row) {
        out[row] = 0.0;
        for (int k = 0; k < 4; ++k) out[row] += a(row, k) * in[k];
    }
    return {out[0], out[1], out[2], out[3]};
}

Mat4d look_at_direction(Vec3d eye, Vec3d direction, Vec3d up) {
    const Vec3d f = normalize(direction);
    const Vec3d s = normalize(cross(f, up));
    const Vec3d u = cross(s, f);
    Mat4d r;
    r(0, 0) = s.x;  r(0, 1) = s.y;  r(0, 2) = s.z;  r(0, 3) = -dot(s, eye);
    r(1, 0) = u.x;  r(1, 1) = u.y;  r(1, 2) = u.z;  r(1, 3) = -dot(u, eye);
    r(2, 0) = -f.x; r(2, 1) = -f.y; r(2, 2) = -f.z; r(2, 3) = dot(f, eye);
    r(3, 0) = 0.0;  r(3, 1) = 0.0;  r(3, 2) = 0.0;  r(3, 3) = 1.0;
    return r;
}

Mat4d perspective(double fov_y, double aspect, double near_plane, double far_plane) {
    const double t = 1.0 / std::tan(fov_y / 2.0);
    Mat4d r;
    r.m.fill(0.0);
    r(0, 0) = t / aspect;
    r(1, 1) = t;
    r(2, 2) = (far_plane + near_plane) / (near_plane - far_plane);
    r(2, 3) = 2.0 * far_plane * near_plane / (near_plane - far_plane);
    r(3, 2) = -1.0;
    return r;
}

Frustum Frustum::from_matrix(const Mat4d& m) {
    // Rows of the matrix combine into the clip planes (row4 +- row_i).
    auto row = [&](int i) { return Vec4d{m(i, 0), m(i, 1), m(i, 2), m(i, 3)}; };
    const Vec4d r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto plane = [](Vec4d a, Vec4d b, double sign) {
        const Vec3d n{a.x + sign * b.x, a.y + sign * b.y, a.z + sign * b.z};
        const double d = a.w + sign * b.w;
        const double l = length(n);
        return Plane{n / l, d / l};
    };
    Frustum f;
    f.planes = {plane(r3, r0, 1.0),  plane(r3, r0, -1.0), plane(r3, r1, 1.0),
                plane(r3, r1, -1.0), plane(r3, r2, 1.0),  plane(r3, r2, -1.0)};
    return f;
}

bool Frustum::intersects_sphere(Vec3d center, double radius) const {
    for (const Plane& p : planes) {
        if (p.distance(center) < -radius) return false;
    }
    return true;
}

}  // namespace ogccdb::runtime
