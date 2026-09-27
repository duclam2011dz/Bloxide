#include "bloxide/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace bloxide {
namespace {
constexpr float Pi = 3.14159265358979323846f;

std::array<float, 3> normalize(const std::array<float, 3>& v) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length <= 0.0001f) return {0.0f, 0.0f, 0.0f};
    return {v[0] / length, v[1] / length, v[2] / length};
}

float dot(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<float, 3> cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
} // namespace

Camera::Camera() : position_{16.0f, 5.5f, 22.0f} {}

std::array<float, 3> Camera::forward() const noexcept {
    const float yaw = yaw_ * Pi / 180.0f;
    const float pitch = pitch_ * Pi / 180.0f;
    return normalize({std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)});
}

std::array<float, 3> Camera::right() const noexcept {
    return normalize(cross(forward(), {0.0f, 1.0f, 0.0f}));
}

void Camera::move(const std::array<float, 3>& delta) noexcept {
    position_[0] += delta[0];
    position_[1] += delta[1];
    position_[2] += delta[2];
}

void Camera::rotate(float yawDelta, float pitchDelta) noexcept {
    yaw_ += yawDelta;
    pitch_ = std::clamp(pitch_ + pitchDelta, -89.0f, 89.0f);
}

std::array<float, 16> Camera::viewMatrix() const noexcept {
    const auto f = forward();
    const auto s = normalize(cross(f, {0.0f, 1.0f, 0.0f}));
    const auto u = cross(s, f);
    return {s[0], u[0], -f[0], 0.0f,
            s[1], u[1], -f[1], 0.0f,
            s[2], u[2], -f[2], 0.0f,
            -dot(s, position_), -dot(u, position_), dot(f, position_), 1.0f};
}

std::array<float, 16> Camera::projectionMatrix(float aspect) const noexcept {
    const float fov = 70.0f * Pi / 180.0f;
    const float f = 1.0f / std::tan(fov / 2.0f);
    const float nearPlane = 0.1f;
    const float farPlane = 200.0f;
    return {f / aspect, 0.0f, 0.0f, 0.0f,
            0.0f, f, 0.0f, 0.0f,
            0.0f, 0.0f, (farPlane + nearPlane) / (nearPlane - farPlane), -1.0f,
            0.0f, 0.0f, (2.0f * farPlane * nearPlane) / (nearPlane - farPlane), 0.0f};
}

} // namespace bloxide
