#pragma once
// =============================================================================
// Math3D.h – Minimal 3D math helpers (Mat4 + vectors)
// =============================================================================

#include "Core.h"

#include <cmath>

namespace myu::engine {

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 1;
};

inline float degToRad(float d) { return d * 0.01745329252f; }

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline Vec3 normalize(const Vec3& v) {
    float len = std::sqrt(dot(v, v));
    if (len <= 0.000001f) return {0, 0, 0};
    return {v.x / len, v.y / len, v.z / len};
}

struct Mat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
};

inline Mat4 identity() { return Mat4(); }

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int rIdx = 0; rIdx < 4; ++rIdx) {
            r.m[c * 4 + rIdx] =
                a.m[0 * 4 + rIdx] * b.m[c * 4 + 0] +
                a.m[1 * 4 + rIdx] * b.m[c * 4 + 1] +
                a.m[2 * 4 + rIdx] * b.m[c * 4 + 2] +
                a.m[3 * 4 + rIdx] * b.m[c * 4 + 3];
        }
    }
    return r;
}

inline Vec4 multiply(const Mat4& m, const Vec4& v) {
    Vec4 r;
    r.x = m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z + m.m[12] * v.w;
    r.y = m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z + m.m[13] * v.w;
    r.z = m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w;
    r.w = m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w;
    return r;
}

inline Mat4 translation(const Vec3& t) {
    Mat4 m = identity();
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

inline Mat4 scale(const Vec3& s) {
    Mat4 m = identity();
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    return m;
}

inline Mat4 rotationX(float deg) {
    float r = degToRad(deg);
    float c = std::cos(r), s = std::sin(r);
    Mat4 m = identity();
    m.m[5] = c;  m.m[9]  = -s;
    m.m[6] = s;  m.m[10] = c;
    return m;
}

inline Mat4 rotationY(float deg) {
    float r = degToRad(deg);
    float c = std::cos(r), s = std::sin(r);
    Mat4 m = identity();
    m.m[0] = c;  m.m[8] = s;
    m.m[2] = -s; m.m[10] = c;
    return m;
}

inline Mat4 rotationZ(float deg) {
    float r = degToRad(deg);
    float c = std::cos(r), s = std::sin(r);
    Mat4 m = identity();
    m.m[0] = c;  m.m[4] = -s;
    m.m[1] = s;  m.m[5] = c;
    return m;
}

inline Mat4 perspective(float fovDeg, float aspect, float zNear, float zFar) {
    float f = 1.0f / std::tan(degToRad(fovDeg) * 0.5f);
    Mat4 m = {};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (zFar + zNear) / (zNear - zFar);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    m.m[15] = 0.0f;
    return m;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize({center.x - eye.x, center.y - eye.y, center.z - eye.z});
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);

    Mat4 m = identity();
    m.m[0] = s.x; m.m[4] = s.y; m.m[8]  = s.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9]  = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;

    Mat4 t = translation({-eye.x, -eye.y, -eye.z});
    return multiply(m, t);
}

} // namespace myu::engine
