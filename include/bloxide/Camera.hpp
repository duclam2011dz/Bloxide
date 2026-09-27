#pragma once

#include <array>

namespace bloxide {

class Camera {
public:
    Camera();

    [[nodiscard]] const std::array<float, 3>& position() const noexcept { return position_; }
    [[nodiscard]] std::array<float, 3> forward() const noexcept;
    [[nodiscard]] std::array<float, 3> right() const noexcept;
    [[nodiscard]] std::array<float, 16> viewMatrix() const noexcept;
    [[nodiscard]] std::array<float, 16> projectionMatrix(float aspect) const noexcept;

    void setPosition(const std::array<float, 3>& position) noexcept { position_ = position; }
    void move(const std::array<float, 3>& delta) noexcept;
    void rotate(float yawDelta, float pitchDelta) noexcept;

private:
    std::array<float, 3> position_{};
    float yaw_ = -90.0f;
    float pitch_ = -18.0f;
};

} // namespace bloxide
