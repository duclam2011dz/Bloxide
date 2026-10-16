#include <bloxide/BuildInfo.hpp>
#include <bloxide/Noise.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include "NoiseSimd.hpp"
#ifdef _MSC_VER
#include <intrin.h>
#endif
namespace bloxide {
namespace {
std::size_t product(std::size_t a, std::size_t b) {
    if (a && b > std::numeric_limits<std::size_t>::max() / a)
        throw std::overflow_error("noise grid dimensions overflow");
    return a * b;
}
void coordinate(double first, double step, std::size_t count) {
    if (!std::isfinite(first) || !std::isfinite(step))
        throw std::invalid_argument("nonfinite noise grid");
    double last = first + (count ? double(count - 1) : 0) * step;
    if (!std::isfinite(last) || std::max(std::abs(first), std::abs(last)) > 17592186044416.0)
        throw std::out_of_range("noise grid out of domain");
}
void backend(NoiseBackend b) {
    if (!noiseBackendSupported(b))
        throw std::invalid_argument("noise backend unsupported by CPU/OS");
}
} // namespace
bool noiseBackendSupported(NoiseBackend b) noexcept {
    if (b == NoiseBackend::Scalar || b == NoiseBackend::Auto)
        return true;
    if (b == NoiseBackend::SSE2 && !BLOXIDE_HAS_SSE2)
        return false;
    if (b == NoiseBackend::AVX2 && !BLOXIDE_HAS_AVX2)
        return false;
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    return b == NoiseBackend::SSE2   ? __builtin_cpu_supports("sse2")
           : b == NoiseBackend::AVX2 ? __builtin_cpu_supports("avx2")
                                     : false;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int r[4];
    __cpuid(r, 1);
    if (b == NoiseBackend::SSE2)
        return (r[3] & (1 << 26)) != 0;
    if ((r[2] & (1 << 27)) == 0 || (r[2] & (1 << 28)) == 0 || (_xgetbv(0) & 6) != 6)
        return false;
    __cpuidex(r, 7, 0);
    return b == NoiseBackend::AVX2 && (r[1] & (1 << 5)) != 0;
#else
    return false;
#endif
}
const char* noiseBackendName(NoiseBackend b) noexcept {
    switch (b) {
    case NoiseBackend::Scalar:
        return "scalar";
    case NoiseBackend::SSE2:
        return "sse2";
    case NoiseBackend::AVX2:
        return "avx2";
    default:
        return "auto (scalar)";
    }
}
void SimplexNoise2D::sampleGrid2D(const NoiseGrid2D& g, double* out, std::size_t capacity,
                                  NoiseBackend b) const {
    auto count = product(g.width, g.depth);
    if (count > capacity || (!out && count))
        throw std::invalid_argument("noise output capacity");
    coordinate(g.x, g.dx, g.width);
    coordinate(g.z, g.dz, g.depth);
    backend(b);
#if BLOXIDE_HAS_SSE2
    if (b == NoiseBackend::SSE2) {
        grid2DSse2(*this, g, out);
        return;
    }
#endif
#if BLOXIDE_HAS_AVX2
    if (b == NoiseBackend::AVX2) {
        grid2DAvx2(*this, g, out);
        return;
    }
#endif
    for (std::size_t z = 0; z < g.depth; ++z)
        for (std::size_t x = 0; x < g.width; ++x)
            out[z * g.width + x] = sample(g.x + double(x) * g.dx, g.z + double(z) * g.dz);
}
void SimplexNoise3D::sampleGrid3D(const NoiseGrid3D& g, double* out, std::size_t capacity,
                                  NoiseBackend b) const {
    auto count = product(product(g.width, g.depth), g.height);
    if (count > capacity || (!out && count))
        throw std::invalid_argument("noise output capacity");
    coordinate(g.x, g.dx, g.width);
    coordinate(g.y, g.dy, g.height);
    coordinate(g.z, g.dz, g.depth);
    backend(b);
#if BLOXIDE_HAS_SSE2
    if (b == NoiseBackend::SSE2) {
        grid3DSse2(*this, g, out);
        return;
    }
#endif
#if BLOXIDE_HAS_AVX2
    if (b == NoiseBackend::AVX2) {
        grid3DAvx2(*this, g, out);
        return;
    }
#endif
    for (std::size_t y = 0; y < g.height; ++y)
        for (std::size_t z = 0; z < g.depth; ++z)
            for (std::size_t x = 0; x < g.width; ++x)
                out[(y * g.depth + z) * g.width + x] =
                    sample(g.x + double(x) * g.dx, g.y + double(y) * g.dy, g.z + double(z) * g.dz);
}
void SimplexNoise3D::samplePoints3D(const double* x, const double* y, const double* z,
                                    std::size_t count, double* out, std::size_t capacity,
                                    NoiseBackend b) const {
    if (count > capacity || (!x && count) || (!y && count) || (!z && count) || (!out && count))
        throw std::invalid_argument("noise point buffers/capacity");
    backend(b);
    for (std::size_t i = 0; i < count; ++i) {
        coordinate(x[i], 0, 1);
        coordinate(y[i], 0, 1);
        coordinate(z[i], 0, 1);
    }
#if BLOXIDE_HAS_SSE2
    if (b == NoiseBackend::SSE2) {
        points3DSse2(*this, x, y, z, count, out);
        return;
    }
#endif
#if BLOXIDE_HAS_AVX2
    if (b == NoiseBackend::AVX2) {
        points3DAvx2(*this, x, y, z, count, out);
        return;
    }
#endif
    for (std::size_t i = 0; i < count; ++i)
        out[i] = sample(x[i], y[i], z[i]);
}
FractalKernel::FractalKernel(const FractalSettings& s) {
    s.validate();
    double f = s.frequency, a = 1, total = 0;
    for (int i = 0; i < s.octaves && a != 0; ++i) {
        frequency_[i] = f;
        amplitude_[i] = a;
        total += a;
        normalization_[i] = total;
        ++count_;
        f *= s.lacunarity;
        a *= s.gain;
    }
}
unsigned FractalKernel::usedOctaves(double footprint) const {
    if (!std::isfinite(footprint) || footprint <= 0)
        throw std::domain_error("invalid sample footprint");
    unsigned used = 0;
    while (used < count_ && frequency_[used] <= 0.5 / footprint)
        ++used;
    return used;
}
template <class Sample>
double FractalKernel::apply(double footprint, FractalMode mode, Sample sample) const {
    auto used = usedOctaves(footprint);
    double value = 0, feedback = 1;
    for (unsigned i = 0; i < used; ++i) {
        double n = sample(frequency_[i]), v = n;
        if (mode == FractalMode::Billow)
            v = 2 * std::abs(n) - 1;
        if (mode == FractalMode::Turbulence)
            v = std::abs(n);
        if (mode == FractalMode::Ridged) {
            v = 1 - std::abs(n);
            v = v * v * feedback;
            feedback = std::clamp(v * 2, 0.0, 1.0);
        }
        value += amplitude_[i] * v;
    }
    return used ? value / normalization_[used - 1] : 0;
}
double FractalKernel::sample2D(const SimplexNoise2D& n, double x, double z, FractalMode mode,
                               double footprint) const {
    coordinate(x, 0, 1);
    coordinate(z, 0, 1);
    return apply(footprint, mode, [&](double f) { return n.sample(x * f, z * f); });
}
double FractalKernel::sample3D(const SimplexNoise3D& n, double x, double y, double z,
                               FractalMode mode, double footprint) const {
    coordinate(x, 0, 1);
    coordinate(y, 0, 1);
    coordinate(z, 0, 1);
    return apply(footprint, mode, [&](double f) { return n.sample(x * f, y * f, z * f); });
}
void FractalKernel::samplePoints3D(const SimplexNoise3D& n, const double* x, const double* y,
                                   const double* z, std::size_t count, double* out,
                                   std::size_t capacity, FractalMode mode, double footprint,
                                   NoiseBackend b) const {
    if (count > capacity || (!x && count) || (!y && count) || (!z && count) || (!out && count))
        throw std::invalid_argument("fractal point buffers/capacity");
    for (std::size_t i = 0; i < count; ++i) {
        coordinate(x[i], 0, 1);
        coordinate(y[i], 0, 1);
        coordinate(z[i], 0, 1);
    }
    auto used = usedOctaves(footprint);
    struct Buffers {
        std::vector<double> x, y, z, samples, feedback;
    };
    thread_local Buffers scratch;
    scratch.x.resize(count);
    scratch.y.resize(count);
    scratch.z.resize(count);
    scratch.samples.resize(count);
    scratch.feedback.assign(count, 1);
    std::fill_n(out, count, 0);
    for (unsigned octave = 0; octave < used; ++octave) {
        for (std::size_t i = 0; i < count; ++i) {
            scratch.x[i] = x[i] * frequency_[octave];
            scratch.y[i] = y[i] * frequency_[octave];
            scratch.z[i] = z[i] * frequency_[octave];
        }
        n.samplePoints3D(scratch.x.data(), scratch.y.data(), scratch.z.data(), count,
                         scratch.samples.data(), count, b);
        for (std::size_t i = 0; i < count; ++i) {
            double v = scratch.samples[i];
            if (mode == FractalMode::Billow)
                v = 2 * std::abs(v) - 1;
            if (mode == FractalMode::Turbulence)
                v = std::abs(v);
            if (mode == FractalMode::Ridged) {
                v = 1 - std::abs(v);
                v = v * v * scratch.feedback[i];
                scratch.feedback[i] = std::clamp(v * 2, 0.0, 1.0);
            }
            out[i] += amplitude_[octave] * v;
        }
    }
    if (used)
        for (std::size_t i = 0; i < count; ++i)
            out[i] /= normalization_[used - 1];
}
} // namespace bloxide
