#include <inttypes.h>
#include <stddef.h>

constexpr size_t CACHE_LINE_BYTES = 64;

/**
 * The buffer used in the test is guaranteed to have
 * at least this many extra bytes beyond the nominal
 * size.
 */
constexpr size_t BUFFER_TAIL_BYTES = 128;

using buf_elem = int;
using cal_f = void(buf_elem* buf, size_t bufsz);

cal_f memset0;
cal_f memset1;
cal_f fill0;
cal_f fill1;
cal_f filln1;
cal_f avx0;
cal_f avx1;
cal_f avx01;
cal_f avx10;
cal_f fill00;
cal_f fill01;
cal_f fill11;
cal_f count0;
cal_f count1;
cal_f one_per0;
cal_f one_per1;
cal_f dp0;
cal_f dp1;
cal_f std_memcpy;

/**
 * These versions written with 256-bit and 512-bit intrinsics
 * so they don't rely on compiler/march.
 */
cal_f fill256_0;
cal_f fill256_1;
#ifdef __AVX512F__
cal_f fill512_0;
cal_f fill512_1;
#endif

