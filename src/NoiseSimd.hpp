#pragma once
#include <bloxide/Noise.hpp>
namespace bloxide {
struct NoiseBatchAccess {
    static std::uint64_t seed(const SimplexNoise2D& n) {
        return n.seed_;
    }
    static std::uint64_t seed(const SimplexNoise3D& n) {
        return n.seed_;
    }
    static const std::array<unsigned char, 256>& permutation(const SimplexNoise2D& n) {
        return n.permutation_;
    }
    static const std::array<unsigned char, 256>& permutation(const SimplexNoise3D& n) {
        return n.permutation_;
    }
};
void grid2DSse2(const SimplexNoise2D&, const NoiseGrid2D&, double*);
void grid3DSse2(const SimplexNoise3D&, const NoiseGrid3D&, double*);
void grid2DAvx2(const SimplexNoise2D&, const NoiseGrid2D&, double*);
void grid3DAvx2(const SimplexNoise3D&, const NoiseGrid3D&, double*);
void points3DSse2(const SimplexNoise3D&, const double*, const double*, const double*, std::size_t,
                  double*);
void points3DAvx2(const SimplexNoise3D&, const double*, const double*, const double*, std::size_t,
                  double*);
} // namespace bloxide
