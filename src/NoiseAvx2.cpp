#include "NoiseSimd.hpp"
#include <immintrin.h>
namespace {
using BatchVector=__m256d;constexpr int BatchLanes=4;
auto bset(double x){return _mm256_set1_pd(x);}auto bload(const double*x){return _mm256_loadu_pd(x);}void bstore(double*x,BatchVector v){_mm256_storeu_pd(x,v);}
auto badd(BatchVector a,BatchVector b){return _mm256_add_pd(a,b);}auto bsub(BatchVector a,BatchVector b){return _mm256_sub_pd(a,b);}
auto bmul(BatchVector a,BatchVector b){return _mm256_mul_pd(a,b);}auto bmin(BatchVector a,BatchVector b){return _mm256_min_pd(a,b);}
auto bmax(BatchVector a,BatchVector b){return _mm256_max_pd(a,b);}auto bgt(BatchVector a,BatchVector b){return _mm256_cmp_pd(a,b,_CMP_GT_OQ);}
auto band(BatchVector a,BatchVector b){return _mm256_and_pd(a,b);}
}
#define BatchGrid2D grid2DAvx2
#define BatchGrid3D grid3DAvx2
#define BatchPoints3D points3DAvx2
#include "NoiseSimdKernel.inl"
