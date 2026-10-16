#pragma once

#include <array>
#include <bloxide/Coordinates.hpp>

namespace bloxide {

class Camera {
  public:
    Camera();

    [[nodiscard]] const std::array<float, 3>& position() const noexcept {
        return position_;
    }
    [[nodiscard]] std::array<float, 3> forward() const noexcept;
    [[nodiscard]] std::array<float, 3> right() const noexcept;
    [[nodiscard]] std::array<float, 16> viewMatrix() const noexcept;
    [[nodiscard]] std::array<float, 16> projectionMatrix(float aspect) const noexcept;

    void setPosition(const std::array<float, 3>& position) noexcept {
        position_ = position;
    }
    void move(const std::array<float, 3>& delta) noexcept;
    void rotate(float yawDelta, float pitchDelta) noexcept;
    std::array<float, 2> orientation() const noexcept {
        return {yaw_, pitch_};
    }
    void setOrigin(ChunkCoord origin) noexcept {
        origin_ = origin;
    }
    ChunkCoord origin() const noexcept {
        return origin_;
    }
    WorldPosition worldPosition() const {
        return {origin_, position_[0], position_[1], position_[2]};
    }

  private:
    std::array<float, 3> position_{};
    float yaw_ = -90.0f;
    float pitch_ = -18.0f;
    ChunkCoord origin_{};
};

} // namespace bloxide
