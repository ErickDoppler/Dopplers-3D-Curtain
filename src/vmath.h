// Small vector / matrix helpers (Z-up world, right handed). Matrices are column-major like OpenGL.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(const Vec3& a) { float l = length(a); return l > 1e-20f ? a / l : Vec3(0, 0, 1); }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
const float kPi = 3.14159265358979f;
inline float rad(float d) { return d * kPi / 180.0f; }

struct Mat4 {
    float m[16];
    Mat4() { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0f : 0.0f; }
    static Mat4 translate(const Vec3& t) { Mat4 r; r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static Mat4 scale(const Vec3& s) { Mat4 r; r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
    static Mat4 rotX(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c; return r; }
    static Mat4 rotY(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c; return r; }
    static Mat4 rotZ(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[4] = -s; r.m[1] = s; r.m[5] = c; return r; }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
            float s = 0; for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
        return r;
    }
    Vec3 point(const Vec3& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12], m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13], m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]};
    }
    Vec3 dir(const Vec3& v) const { return {m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z, m[2] * v.x + m[6] * v.y + m[10] * v.z}; }
};
