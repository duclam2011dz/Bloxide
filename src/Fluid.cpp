#include <bloxide/Fluid.hpp>
#include <bloxide/Noise.hpp>
#include <bloxide/Coordinates.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <bloxide/Trace.hpp>
namespace bloxide {
namespace {
constexpr int neighbors[6][3]{{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
Int3 offset(Int3 p, int x, int y, int z) {
    p.x += x;
    p.y += y;
    p.z += z;
    return p;
}
bool order(const Int3& a, const Int3& b) {
    return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
}
bool water(BlockState b) {
    return b == BlockType::Water;
}
} // namespace
std::size_t VoxelCoordHash::operator()(const Int3& p) const noexcept {
    return std::size_t(SeedDerivation::coordinateHash(0, p.x, p.y, p.z));
}
bool FluidEngine::active(Int3 p) const {
    if (p.x < -WorldLimit || p.x >= WorldLimit || p.z < -WorldLimit || p.z >= WorldLimit ||
        p.y <= 0 || p.y >= 256)
        return false;
    auto c = blockToChunk(p.x, p.z);
    return std::max(std::llabs(c.x - center_.x), std::llabs(c.z - center_.z)) <= radius_;
}
void FluidEngine::seed(Int3 p) {
    if (p.y > 0 && p.y < 256 && p.x >= -WorldLimit && p.x < WorldLimit && p.z >= -WorldLimit &&
        p.z < WorldLimit)
        (active(p) ? frontier_ : frozen_).insert(p);
}
void FluidEngine::notify(Int3 p) {
    seed(p);
    for (auto& n : neighbors)
        seed(offset(p, n[0], n[1], n[2]));
}
void FluidEngine::setRegion(ChunkCoord center, int radius) {
    center_ = center;
    radius_ = std::clamp(radius, 0, 6);
    for (auto it = frontier_.begin(); it != frontier_.end();)
        if (!active(*it)) {
            frozen_.insert(*it);
            it = frontier_.erase(it);
        } else
            ++it;
    for (auto it = frozen_.begin(); it != frozen_.end();)
        if (active(*it)) {
            frontier_.insert(*it);
            it = frozen_.erase(it);
        } else
            ++it;
}
std::vector<FluidUpdate> FluidEngine::tick(const Query& query) {
    TraceScope trace(TracePhase::Fluid, center_);
    auto started = std::chrono::steady_clock::now();
    std::vector<FluidUpdate> updates;
    std::vector<Int3> candidates(frontier_.begin(), frontier_.end());
    frontier_.clear();
    std::sort(candidates.begin(), candidates.end(), order);
    auto read = [&](Int3 p) {
        if (!active(p))
            return FluidCell{BlockType::Bedrock, false};
        auto c = query(p);
        if (!c.loaded)
            c.state = BlockType::Bedrock;
        return c;
    };
    for (auto p : candidates) {
        auto current = read(p);
        if (!current.loaded) {
            frozen_.insert(p);
            continue;
        }
        auto old = current.state;
        if (!isReplaceable(old.type()))
            continue;
        if (old.source())
            continue;
        auto above = read(offset(p, 0, 1, 0)).state, below = read(offset(p, 0, -1, 0)).state;
        BlockState next{};
        if (water(above))
            next = BlockState::water(above.waterLevel(), true);
        else {
            unsigned best = 8, sources = 0;
            for (int i : {0, 1, 4, 5}) {
                auto& n = neighbors[i];
                auto otherPos = offset(p, n[0], 0, n[2]);
                auto other = read(otherPos).state;
                if (!water(other))
                    continue;
                if (other.source())
                    ++sources;
                auto support = read(offset(otherPos, 0, -1, 0)).state;
                if (isSolid(support.type()) || (water(support) && !support.falling()))
                    best = std::min(best, other.waterLevel() + 1);
            }
            if (sources >= 2 && (isSolid(below.type()) || below.source()))
                next = BlockState::water();
            else if (best <= 7)
                next = BlockState::water(best);
        }
        if (next == BlockType::Air && old != BlockType::Water)
            next = old;
        if (next != old) {
            updates.push_back({p, old, next});
            if (next.source() && !old.source())
                ++stats_.sourceConversions;
        }
    }
    for (auto& u : updates)
        notify(u.position);
    ++stats_.ticks;
    stats_.updates += updates.size();
    stats_.lastMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    stats_.totalMs += stats_.lastMs;
    return updates;
}
std::vector<FluidUpdate>
FluidEngine::advance(double dt, const Query& query,
                     const std::function<void(const std::vector<FluidUpdate>&)>& apply) {
    if (!std::isfinite(dt) || dt < 0)
        throw std::invalid_argument("invalid fluid timestep");
    accumulator_ += dt;
    std::vector<FluidUpdate> all;
    for (int i = 0; i < 4 && accumulator_ + 1e-10 >= 0.1; ++i) {
        auto changes = tick(query);
        apply(changes);
        all.insert(all.end(), changes.begin(), changes.end());
        accumulator_ = std::max(0.0, accumulator_ - 0.1);
    }
    return all;
}
FluidStats FluidEngine::stats() const {
    auto s = stats_;
    s.frontier = frontier_.size();
    s.frozen = frozen_.size();
    s.debtSeconds = accumulator_;
    return s;
}
} // namespace bloxide
