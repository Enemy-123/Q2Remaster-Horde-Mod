// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#pragma once
#include <cassert>
#include <type_traits>


using nullptr_t = std::nullptr_t;

struct vec3_t
{
    float x, y, z;

    // ========================================================================
    // OPTIMIZATION: Removed try/throw.
    // This allows MSVC to auto-vectorize loops using this class.
    // ========================================================================
    [[nodiscard]] constexpr const float& operator[](size_t i) const
    {
        assert(i < 3 && "vec3_t index out of bounds");
        return (&x)[i];
    }

    [[nodiscard]] constexpr float& operator[](size_t i)
    {
        assert(i < 3 && "vec3_t index out of bounds");
        return (&x)[i];
    }

    // ========================================================================
    // C-INTEROP: Allows passing vec3_t to Quake 2 C functions (gi.trace, etc)
    // ========================================================================
    [[nodiscard]] float* data() { return &x; }
    [[nodiscard]] const float* data() const { return &x; }
    operator float* () { return &x; }
    operator const float* () const { return &x; }

    // ========================================================================
    // COMPARISON
    // ========================================================================
    [[nodiscard]] constexpr bool equals(const vec3_t& v) const
    {
        return x == v.x && y == v.y && z == v.z;
    }
    [[nodiscard]] inline bool equals(const vec3_t& v, float epsilon) const
    {
        return std::abs(x - v.x) <= epsilon &&
               std::abs(y - v.y) <= epsilon &&
               std::abs(z - v.z) <= epsilon;
    }
    [[nodiscard]] constexpr bool operator==(const vec3_t& v) const { return equals(v); }
    [[nodiscard]] constexpr bool operator!=(const vec3_t& v) const { return !(*this == v); }
    [[nodiscard]] constexpr explicit operator bool() const { return x || y || z; }

    // ========================================================================
    // ARITHMETIC
    // ========================================================================
    [[nodiscard]] constexpr float dot(const vec3_t& v) const
    {
        return (x * v.x) + (y * v.y) + (z * v.z);
    }

    // Component-wise scaling (kept for compatibility)
    [[nodiscard]] constexpr vec3_t scaled(const vec3_t& v) const
    {
        return { x * v.x, y * v.y, z * v.z };
    }
    constexpr vec3_t& scale(const vec3_t& v)
    {
        *this = this->scaled(v);
        return *this;
    }

    [[nodiscard]] constexpr vec3_t operator-(const vec3_t& v) const { return { x - v.x, y - v.y, z - v.z }; }
    [[nodiscard]] constexpr vec3_t operator+(const vec3_t& v) const { return { x + v.x, y + v.y, z + v.z }; }
    [[nodiscard]] constexpr vec3_t operator/(const vec3_t& v) const { return { x / v.x, y / v.y, z / v.z }; }
    [[nodiscard]] constexpr vec3_t operator*(const vec3_t& v) const { return { x * v.x, y * v.y, z * v.z }; }

    [[nodiscard]] constexpr vec3_t operator*(float v) const { return { x * v, y * v, z * v }; }
    [[nodiscard]] constexpr vec3_t operator/(float v) const { float inv = 1.0f / v; return { x * inv, y * inv, z * inv }; }
    [[nodiscard]] constexpr vec3_t operator-() const { return { -x, -y, -z }; }

    constexpr vec3_t& operator-=(const vec3_t& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    constexpr vec3_t& operator+=(const vec3_t& v) { x += v.x; y += v.y; z += v.z; return *this; }
    constexpr vec3_t& operator*=(float v) { x *= v; y *= v; z *= v; return *this; }
    constexpr vec3_t& operator/=(float v) { float inv = 1.0f / v; x *= inv; y *= inv; z *= inv; return *this; }

    // ========================================================================
    // MATH OPERATIONS
    // ========================================================================
    [[nodiscard]] constexpr float lengthSquared() const
    {
        return (x * x) + (y * y) + (z * z);
    }

    // Removed constexpr (std::sqrt is not strictly constexpr in all C++ versions yet)
    [[nodiscard]] inline float length() const
    {
        return std::sqrt(lengthSquared());
    }

    [[nodiscard]] inline vec3_t normalized() const
    {
        float len = length();
        return len > 1e-6f ? (*this * (1.f / len)) : *this;
    }

    // Overload that returns the length via reference parameter
    [[nodiscard]] inline vec3_t normalized(float& len) const
    {
        len = length();
        return len > 1e-6f ? (*this * (1.f / len)) : *this;
    }

    inline float normalize()
    {
        float len = length();
        if (len > 1e-6f)
            *this *= (1.f / len);
        return len;
    }

    [[nodiscard]] constexpr vec3_t cross(const vec3_t& v) const
    {
        return {
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        };
    }
};

constexpr vec3_t vec3_origin{};

// ============================================================================
// Helper Functions
// ============================================================================

// Missing from original code, needed for slerp
[[nodiscard]] constexpr vec3_t lerp(const vec3_t& a, const vec3_t& b, float t) {
    return a + (b - a) * t;
}

[[nodiscard]] inline bool is_valid_vector(const vec3_t& vec) {
    return !std::isnan(vec.x) && !std::isnan(vec.y) && !std::isnan(vec.z);
}

[[nodiscard]] inline vec3_t safe_normalized(const vec3_t& vec) {
    float const len = vec.length();
    if (len < 1e-6f || std::isnan(len)) {
        return vec3_origin;
    }
    return vec * (1.0f / len);
}

inline void AngleVectors(const vec3_t& angles, vec3_t* forward, vec3_t* right, vec3_t* up)
{
    float angle = angles[YAW] * (PIf * 2 / 360);
    float const sy = std::sin(angle);
    float const cy = std::cos(angle);
    angle = angles[PITCH] * (PIf * 2 / 360);
    float const sp = std::sin(angle);
    float const cp = std::cos(angle);
    angle = angles[ROLL] * (PIf * 2 / 360);
    float const sr = std::sin(angle);
    float const cr = std::cos(angle);

    if (forward) {
        forward->x = cp * cy;
        forward->y = cp * sy;
        forward->z = -sp;
    }
    if (right) {
        right->x = (-1 * sr * sp * cy + -1 * cr * -sy);
        right->y = (-1 * sr * sp * sy + -1 * cr * cy);
        right->z = -1 * sr * cp;
    }
    if (up) {
        up->x = (cr * sp * cy + -sr * -sy);
        up->y = (cr * sp * sy + -sr * cy);
        up->z = cr * cp;
    }
}

struct angle_vectors_t {
    vec3_t forward, right, up;
};

// C++ Overloads for AngleVectors
inline void AngleVectors(const vec3_t& angles, vec3_t& forward, vec3_t& right, vec3_t& up) {
    AngleVectors(angles, &forward, &right, &up);
}

// for destructuring
inline angle_vectors_t AngleVectors(const vec3_t& angles)
{
    angle_vectors_t v{};
    AngleVectors(angles, &v.forward, &v.right, &v.up);
    return v;
}

// silly wrappers to allow old C code to work
inline void AngleVectors(const vec3_t& angles, vec3_t& forward, vec3_t& right, nullptr_t)
{
    AngleVectors(angles, &forward, &right, nullptr);
}
inline void AngleVectors(const vec3_t& angles, vec3_t& forward, nullptr_t, vec3_t& up)
{
    AngleVectors(angles, &forward, nullptr, &up);
}
inline void AngleVectors(const vec3_t& angles, vec3_t& forward, nullptr_t, nullptr_t)
{
    AngleVectors(angles, &forward, nullptr, nullptr);
}
inline void AngleVectors(const vec3_t& angles, nullptr_t, nullptr_t, vec3_t& up)
{
    AngleVectors(angles, nullptr, nullptr, &up);
}
inline void AngleVectors(const vec3_t& angles, nullptr_t, vec3_t& right, nullptr_t)
{
    AngleVectors(angles, nullptr, &right, nullptr);
}

inline void ClearBounds(vec3_t& mins, vec3_t& maxs)
{
    mins[0] = mins[1] = mins[2] = std::numeric_limits<float>::infinity();
    maxs[0] = maxs[1] = maxs[2] = -std::numeric_limits<float>::infinity();
}

inline void AddPointToBounds(const vec3_t& v, vec3_t& mins, vec3_t& maxs)
{
    if (v.x < mins.x) mins.x = v.x;
    if (v.x > maxs.x) maxs.x = v.x;
    if (v.y < mins.y) mins.y = v.y;
    if (v.y > maxs.y) maxs.y = v.y;
    if (v.z < mins.z) mins.z = v.z;
    if (v.z > maxs.z) maxs.z = v.z;
}

[[nodiscard]] constexpr vec3_t ProjectPointOnPlane(const vec3_t& p, const vec3_t& normal)
{
    float const inv_denom = 1.0f / normal.dot(normal);
    float const d = normal.dot(p) * inv_denom;
    return p - ((normal * inv_denom) * d);
}

[[nodiscard]] inline vec3_t PerpendicularVector(const vec3_t& src)
{
    int    pos = 0;
    float minelem = 1.0F;
    vec3_t tempvec{};

    for (int i = 0; i < 3; i++) {
        if (std::abs(src[i]) < minelem) {
            pos = i;
            minelem = std::abs(src[i]);
        }
    }
    tempvec[pos] = 1.0F;
    return ProjectPointOnPlane(tempvec, src).normalized();
}

using mat3_t = std::array<std::array<float, 3>, 3>;

[[nodiscard]] constexpr mat3_t R_ConcatRotations(const mat3_t& in1, const mat3_t& in2)
{
    return {
        std::array<float, 3> {
            in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] + in1[0][2] * in2[2][0],
            in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] + in1[0][2] * in2[2][1],
            in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] + in1[0][2] * in2[2][2]
        },
        {
            in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] + in1[1][2] * in2[2][0],
            in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] + in1[1][2] * in2[2][1],
            in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] + in1[1][2] * in2[2][2]
        },
        {
            in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] + in1[2][2] * in2[2][0],
            in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] + in1[2][2] * in2[2][1],
            in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] + in1[2][2] * in2[2][2]
        }
    };
}

[[nodiscard]] inline vec3_t RotatePointAroundVector(const vec3_t& dir, const vec3_t& point, float degrees)
{
    mat3_t  m{};
    mat3_t  im;
    mat3_t  zrot;
    mat3_t  rot;
    vec3_t vr, vup, vf;

    vf = dir;

    vr = PerpendicularVector(dir);
    vup = vr.cross(vf);

    m[0][0] = vr[0];
    m[1][0] = vr[1];
    m[2][0] = vr[2];

    m[0][1] = vup[0];
    m[1][1] = vup[1];
    m[2][1] = vup[2];

    m[0][2] = vf[0];
    m[1][2] = vf[1];
    m[2][2] = vf[2];

    im = m;

    im[0][1] = m[1][0];
    im[0][2] = m[2][0];
    im[1][0] = m[0][1];
    im[1][2] = m[2][1];
    im[2][0] = m[0][2];
    im[2][1] = m[1][2];

    zrot = {};
    zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0F;

    zrot[0][0] = std::cos(DEG2RAD(degrees));
    zrot[0][1] = std::sin(DEG2RAD(degrees));
    zrot[1][0] = -std::sin(DEG2RAD(degrees));
    zrot[1][1] = std::cos(DEG2RAD(degrees));

    rot = R_ConcatRotations(R_ConcatRotations(m, zrot), im);

    return {
        rot[0][0] * point[0] + rot[0][1] * point[1] + rot[0][2] * point[2],
        rot[1][0] * point[0] + rot[1][1] * point[1] + rot[1][2] * point[2],
        rot[2][0] * point[0] + rot[2][1] * point[1] + rot[2][2] * point[2]
    };
}

[[nodiscard]] inline vec3_t slerp(const vec3_t& from, const vec3_t& to, float t)
{
    float const dot = from.dot(to);

    if (dot >= 0.9995f) {
        return lerp(from, to, t);
    }
    else if (dot <= -0.9995f) {
        vec3_t const c = vec3_t{ 1.0f, 0.0f, 0.0f }.cross(to);
        if (t <= 0.5f) return lerp(from, c, t * 2);
        else return lerp(c, to, (t - 0.5f) * 2);
    }

    float const ang = std::acos(dot);
    float const sinOmega = std::sin(ang);
    float const sinAOmega = std::sin((1.0f - t) * ang);
    float const sinBOmega = std::sin(t * ang);

    return (from * sinAOmega + to * sinBOmega) / sinOmega;
}

[[nodiscard]] constexpr vec3_t closest_point_to_box(const vec3_t& from, const vec3_t& absmins, const vec3_t& absmaxs)
{
    return {
        (from[0] < absmins[0]) ? absmins[0] : (from[0] > absmaxs[0]) ? absmaxs[0] : from[0],
        (from[1] < absmins[1]) ? absmins[1] : (from[1] > absmaxs[1]) ? absmaxs[1] : from[1],
        (from[2] < absmins[2]) ? absmins[2] : (from[2] > absmaxs[2]) ? absmaxs[2] : from[2]
    };
}

[[nodiscard]] inline float distance_between_boxes(const vec3_t& absminsa, const vec3_t& absmaxsa,
    const vec3_t& absminsb, const vec3_t& absmaxsb) {
    float len_squared = 0.0f;
    float d;

    // Calculate difference on x-axis
    if (absmaxsa.x < absminsb.x) {
        d = absminsb.x - absmaxsa.x;
        len_squared += d * d;
    }
    else if (absminsa.x > absmaxsb.x) {
        d = absminsa.x - absmaxsb.x;
        len_squared += d * d;
    } // else: overlap on X, contribution is 0

    // Calculate difference on y-axis
    if (absmaxsa.y < absminsb.y) {
        d = absminsb.y - absmaxsa.y;
        len_squared += d * d;
    }
    else if (absminsa.y > absmaxsb.y) {
        d = absminsa.y - absmaxsb.y;
        len_squared += d * d;
    } // else: overlap on Y, contribution is 0

    // Calculate difference on z-axis
    if (absmaxsa.z < absminsb.z) {
        d = absminsb.z - absmaxsa.z;
        len_squared += d * d;
    }
    else if (absminsa.z > absmaxsb.z) {
        d = absminsa.z - absmaxsb.z;
        len_squared += d * d;
    } // else: overlap on Z, contribution is 0

    return std::sqrt(len_squared);
}

[[nodiscard]] constexpr bool boxes_intersect(const vec3_t& amins, const vec3_t& amaxs, const vec3_t& bmins, const vec3_t& bmaxs)
{
    return amins.x <= bmaxs.x &&
        amaxs.x >= bmins.x &&
        amins.y <= bmaxs.y &&
        amaxs.y >= bmins.y &&
        amins.z <= bmaxs.z &&
        amaxs.z >= bmins.z;
}

/*
==================
ClipVelocity

Slide off of the impacting object
==================
*/
constexpr float STOP_EPSILON = 0.1f;

[[nodiscard]] constexpr vec3_t ClipVelocity(const vec3_t& in, const vec3_t& normal, float overbounce)
{
    float const dot = in.dot(normal);
    vec3_t out = in + (normal * (-2 * dot));
    out *= overbounce - 1.f;

    if (out.lengthSquared() < STOP_EPSILON * STOP_EPSILON)
        out = {};

    return out;
}

[[nodiscard]] constexpr vec3_t SlideClipVelocity(const vec3_t& in, const vec3_t& normal, float overbounce)
{
    float const backoff = in.dot(normal) * overbounce;
    vec3_t out = in - (normal * backoff);

    for (int i = 0; i < 3; i++)
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
            out[i] = 0;

    return out;
}

[[nodiscard]] inline float vectoyaw(const vec3_t& vec)
{
    // PMM - fixed to correct for pitch of 0
    if (vec[PITCH] == 0)
    {
        if (vec[YAW] == 0)
            return 0.f;
        else if (vec[YAW] > 0)
            return 90.f;
        else
            return 270.f;
    }

    float yaw = (std::atan2(vec[YAW], vec[PITCH]) * (180.f / PIf));

    if (yaw < 0)
        yaw += 360;

    return yaw;
}

[[nodiscard]] inline vec3_t vectoangles(const vec3_t& vec)
{
    float forward;
    float yaw, pitch;

    if (vec[1] == 0 && vec[0] == 0)
    {
        if (vec[2] > 0)
            return { -90.f, 0.f, 0.f };
        else
            return { -270.f, 0.f, 0.f };
    }

    // PMM - fixed to correct for pitch of 0
    if (vec[0])
        yaw = (std::atan2(vec[1], vec[0]) * (180.f / PIf));
    else if (vec[1] > 0)
        yaw = 90;
    else
        yaw = 270;

    if (yaw < 0)
        yaw += 360;

    forward = std::sqrt(vec[0] * vec[0] + vec[1] * vec[1]);
    pitch = (std::atan2(vec[2], forward) * (180.f / PIf));

    if (pitch < 0)
        pitch += 360;

    return { -pitch, yaw, 0 };
}

[[nodiscard]] constexpr vec3_t G_ProjectSource(const vec3_t& point, const vec3_t& distance, const vec3_t& forward, const vec3_t& right)
{
    return point + (forward * distance[0]) + (right * distance[1]) + vec3_t{ 0.f, 0.f, distance[2] };
}

[[nodiscard]] constexpr vec3_t G_ProjectSource2(const vec3_t& point, const vec3_t& distance, const vec3_t& forward, const vec3_t& right, const vec3_t& up)
{
    return point + (forward * distance[0]) + (right * distance[1]) + (up * distance[2]);
}

[[nodiscard]] constexpr vec3_t G_ProjectSourceWithOffset(const vec3_t& point, const vec3_t& distance, const vec3_t& forward, const vec3_t& right, const vec3_t& up, const vec3_t& offset)
{
    return point + (forward * distance[0]) + (right * distance[1]) + (up * distance[2]) + offset;
}

// ============================================================================
// Performance Helpers (For Horde Mode AI)
// ============================================================================

[[nodiscard]] constexpr bool IsCloserThan(const vec3_t& a, const vec3_t& b, float distance) {
    return (b - a).lengthSquared() < (distance * distance);
}

[[nodiscard]] constexpr float DistanceSquared2D(const vec3_t& a, const vec3_t& b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

[[nodiscard]] inline float Distance2D(const vec3_t& a, const vec3_t& b) {
    return std::sqrt(DistanceSquared2D(a, b));
}

[[nodiscard]] constexpr bool IsCloserThan2D(const vec3_t& a, const vec3_t& b, float distance) {
    return DistanceSquared2D(a, b) < (distance * distance);
}

[[nodiscard]] constexpr bool IsWithinRange(const vec3_t& a, const vec3_t& b, float distance) {
    return (b - a).lengthSquared() <= (distance * distance);
}

[[nodiscard]] constexpr bool IsWithinRange2D(const vec3_t& a, const vec3_t& b, float distance) {
    return DistanceSquared2D(a, b) <= (distance * distance);
}

[[nodiscard]] constexpr bool IsFurtherThan(const vec3_t& a, const vec3_t& b, float distance) {
    return (b - a).lengthSquared() > (distance * distance);
}

// Direction to target with safety checks - avoids NaN from zero-length vectors
[[nodiscard]] inline vec3_t DirectionTo(const vec3_t& from, const vec3_t& to) {
    return safe_normalized(to - from);
}

// ============================================================================
// FMT Support (for fmtlib formatting)
// ============================================================================
template<>
struct fmt::formatter<vec3_t> : fmt::formatter<float>
{
    template<typename FormatContext>
    auto format(const vec3_t& p, FormatContext& ctx) const
    {
        auto out = fmt::formatter<float>::format(p.x, ctx);
        out = fmt::format_to(out, " ");
        ctx.advance_to(out);
        out = fmt::formatter<float>::format(p.y, ctx);
        out = fmt::format_to(out, " ");
        ctx.advance_to(out);
        return fmt::formatter<float>::format(p.z, ctx);
    }
};
