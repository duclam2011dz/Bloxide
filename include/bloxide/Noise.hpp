#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace bloxide {
struct NoiseBatchAccess;
class SplitMix64 {
  public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next();
    std::uint64_t bounded(std::uint64_t n);
    static std::uint64_t mix(std::uint64_t x);

  private:
    std::uint64_t state_;
};
enum class SeedDomain : std::uint64_t {
    Continental = 1,
    Ridges,
    Hills,
    Turbulence,
    WarpX,
    WarpZ,
    Caves,
    CaveWarpX,
    CaveWarpY,
    CaveWarpZ,
    Structures,
    Vegetation,
    Roughness,
    MountainMask,
    Valley,
    Plateau,
    Density,
    DensityWarpX,
    DensityWarpY,
    DensityWarpZ,
    RockMask,
    Temperature,
    Humidity,
    Aquifer,
    Iron,
    Gold,
    Diamond,
    Flowers
};
struct NoiseGrid2D {
    double x = 0, z = 0, dx = 1, dz = 1;
    std::size_t width = 0, depth = 0;
};
struct NoiseGrid3D {
    double x = 0, y = 0, z = 0, dx = 1, dy = 1, dz = 1;
    std::size_t width = 0, height = 0, depth = 0;
};
enum class NoiseBackend { Scalar, SSE2, AVX2, Auto };
bool noiseBackendSupported(NoiseBackend) noexcept;
const char* noiseBackendName(NoiseBackend) noexcept;
struct SeedDerivation {
    static std::uint64_t derive(std::uint64_t root, SeedDomain domain);
    static std::uint64_t coordinateHash(std::uint64_t seed, std::int64_t x, std::int64_t y,
                                        std::int64_t z);
};
class SimplexNoise2D {
  public:
    explicit SimplexNoise2D(std::uint64_t seed);
    double sample(double x, double y) const;
    void sampleGrid2D(const NoiseGrid2D&, double* output, std::size_t capacity,
                      NoiseBackend backend = NoiseBackend::Scalar) const;

  private:
    friend struct NoiseBatchAccess;
    std::uint64_t seed_;
    std::array<unsigned char, 256> permutation_{};
};
class SimplexNoise3D {
  public:
    explicit SimplexNoise3D(std::uint64_t seed);
    double sample(double x, double y, double z) const;
    void sampleGrid3D(const NoiseGrid3D&, double* output, std::size_t capacity,
                      NoiseBackend backend = NoiseBackend::Scalar) const;
    void samplePoints3D(const double* x, const double* y, const double* z, std::size_t count,
                        double* output, std::size_t capacity,
                        NoiseBackend backend = NoiseBackend::Scalar) const;

  private:
    friend struct NoiseBatchAccess;
    std::uint64_t seed_;
    std::array<unsigned char, 256> permutation_{};
};
struct FractalSettings {
    int octaves = 5;
    double frequency = 1.0 / 1536, lacunarity = 2, gain = 0.5;
    void validate() const;
};
enum class FractalMode { Fbm, Billow, Turbulence, Ridged };
class FractalKernel {
  public:
    explicit FractalKernel(const FractalSettings&);
    double sample2D(const SimplexNoise2D&, double x, double z, FractalMode mode = FractalMode::Fbm,
                    double footprint = 1) const;
    double sample3D(const SimplexNoise3D&, double x, double y, double z,
                    FractalMode mode = FractalMode::Fbm, double footprint = 1) const;
    void samplePoints3D(const SimplexNoise3D&, const double* x, const double* y, const double* z,
                        std::size_t count, double* output, std::size_t capacity,
                        FractalMode mode = FractalMode::Fbm, double footprint = 1,
                        NoiseBackend backend = NoiseBackend::Scalar) const;
    unsigned usedOctaves(double footprint) const;
    const std::array<double, 12>& frequencies() const {
        return frequency_;
    }

  private:
    std::array<double, 12> frequency_{}, amplitude_{}, normalization_{};
    unsigned count_ = 0;
    template <class Sample> double apply(double footprint, FractalMode mode, Sample sample) const;
};
class FractalNoise {
  public:
    static double sample2D(const SimplexNoise2D& noise, const FractalSettings& settings, double x,
                           double z, FractalMode mode = FractalMode::Fbm, double footprint = 1);
    static double sample3D(const SimplexNoise3D& noise, const FractalSettings& settings, double x,
                           double y, double z, FractalMode mode = FractalMode::Fbm,
                           double footprint = 1);
    static std::array<double, 2> warp2D(const SimplexNoise2D& xField, const SimplexNoise2D& zField,
                                        const FractalSettings& settings, double x, double z,
                                        double strength, double footprint = 1);
    static std::array<double, 3> warp3D(const SimplexNoise3D& xField, const SimplexNoise3D& yField,
                                        const SimplexNoise3D& zField,
                                        const FractalSettings& settings, double x, double y,
                                        double z, double strength, double footprint = 1);
};
} // namespace bloxide
