#pragma once
#include <bloxide/Targeting.hpp>
#include <functional>
#include <unordered_set>
namespace bloxide {
struct VoxelCoordHash {
    std::size_t operator()(const Int3& p) const noexcept;
};
struct FluidCell {
    BlockState state{};
    bool loaded = false;
};
struct FluidUpdate {
    Int3 position{};
    BlockState before{}, after{};
};
struct FluidStats {
    std::uint64_t ticks = 0, updates = 0, sourceConversions = 0;
    std::size_t frontier = 0, frozen = 0;
    double lastMs = 0, totalMs = 0, debtSeconds = 0;
};
class FluidEngine {
  public:
    using Query = std::function<FluidCell(const Int3&)>;
    void notify(Int3);
    void seed(Int3);
    void setRegion(ChunkCoord center, int radius = 6);
    std::vector<FluidUpdate> tick(const Query&);
    std::vector<FluidUpdate>
    advance(double dt, const Query&,
            const std::function<void(const std::vector<FluidUpdate>&)>& apply);
    FluidStats stats() const;

  private:
    bool active(Int3) const;
    std::unordered_set<Int3, VoxelCoordHash> frontier_, frozen_;
    ChunkCoord center_{};
    int radius_ = 6;
    double accumulator_ = 0;
    FluidStats stats_{};
};
} // namespace bloxide
