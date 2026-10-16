#include "NoiseSimd.hpp"
#include <emmintrin.h>
namespace {
using BatchVector = __m128d;
constexpr int BatchLanes = 2;
auto bset(double x) {
    return _mm_set1_pd(x);
}
auto bload(const double* x) {
    return _mm_loadu_pd(x);
}
void bstore(double* x, BatchVector v) {
    _mm_storeu_pd(x, v);
}
auto badd(BatchVector a, BatchVector b) {
    return _mm_add_pd(a, b);
}
auto bsub(BatchVector a, BatchVector b) {
    return _mm_sub_pd(a, b);
}
auto bmul(BatchVector a, BatchVector b) {
    return _mm_mul_pd(a, b);
}
auto bmin(BatchVector a, BatchVector b) {
    return _mm_min_pd(a, b);
}
auto bmax(BatchVector a, BatchVector b) {
    return _mm_max_pd(a, b);
}
auto bgt(BatchVector a, BatchVector b) {
    return _mm_cmpgt_pd(a, b);
}
auto band(BatchVector a, BatchVector b) {
    return _mm_and_pd(a, b);
}
} // namespace
#define BatchGrid2D grid2DSse2
#define BatchGrid3D grid3DSse2
#define BatchPoints3D points3DSse2
#include "NoiseSimdKernel.inl"
