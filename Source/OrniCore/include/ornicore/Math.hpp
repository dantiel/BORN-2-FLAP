#pragma once

#include <cmath>

namespace ornicore {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] constexpr double radians(double degrees) noexcept {
    return degrees * kPi / 180.0;
}

[[nodiscard]] constexpr double clamp(double value, double low, double high) noexcept {
    return value < low ? low : (value > high ? high : value);
}

[[nodiscard]] inline double smoothStep(double edge0, double edge1, double value) noexcept {
    const double t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

struct Vec3 {
    double x{};
    double y{};
    double z{};

    constexpr Vec3& operator+=(const Vec3& rhs) noexcept {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 lhs, const Vec3& rhs) noexcept {
    return lhs += rhs;
}

[[nodiscard]] constexpr Vec3 operator*(const Vec3& value, double scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

}  // namespace ornicore
