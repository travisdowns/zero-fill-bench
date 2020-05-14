#include "common.hpp"
#include "hedley.h"
#include "opt-control.hpp"

#include <algorithm>
#include <assert.h>
#include <string.h>

#ifdef __AVX__
#include <immintrin.h>
#endif

constexpr size_t CL_SIZE = 64; // assumed size of a cache line

void memset_val(buf_elem* buf, size_t size, int c) {
    memset(buf, c, size * sizeof(buf[0]));
    opt_control::sink_ptr(buf);
}

void memset0(buf_elem* buf, size_t size) {
    memset_val(buf, size, 0);
}

void memset1(buf_elem* buf, size_t size) {
    memset_val(buf, size, 1);
}

HEDLEY_NEVER_INLINE
void std_fill(buf_elem* buf, size_t size, buf_elem val) {
    std::fill(buf, buf + size, val);
    opt_control::sink_ptr(buf);
}

void fill0(buf_elem* buf, size_t size) {
    std_fill(buf, size, 0);
}

void fill1(buf_elem* buf, size_t size) {
    std_fill(buf, size, 1);
}

void filln1(buf_elem* buf, size_t size) {
    std_fill(buf, size, -1);
}

HEDLEY_NEVER_INLINE
void avx_fill(buf_elem* buf, size_t size, buf_elem val0, buf_elem val1) {
#ifdef __AVX__

    auto vbuf = (__m256i *)buf;
    __m256i filler0 = _mm256_set1_epi32(val0);
    __m256i filler1 = _mm256_set1_epi32(val1);
    size_t chunks = (size * sizeof(buf_elem) + 31) / 32;
    for (size_t c = 0; c < chunks; c += 4) {
        _mm256_store_si256(vbuf + c + 0, filler0);
        _mm256_store_si256(vbuf + c + 1, filler0);
        _mm256_store_si256(vbuf + c + 2, filler1);
        _mm256_store_si256(vbuf + c + 3, filler1);
    }    
    opt_control::sink_ptr(vbuf);
#else
    (void)buf; (void)size; (void)val0; (void)val1;
    assert(false);
#endif
}

void avx0(buf_elem* buf, size_t size) {
    avx_fill(buf, size, 0, 0);
}

void avx1(buf_elem* buf, size_t size) {
    avx_fill(buf, size, 1, 1);
}

void avx01(buf_elem* buf, size_t size) {
    avx_fill(buf, size, 0, 1);
}

void std_fill2(buf_elem* buf, size_t size, buf_elem val0, buf_elem val1) {
    std_fill(buf, size, val0);
    std_fill(buf, size, val1);
}

void fill00(buf_elem* buf, size_t size) {
    std_fill2(buf, size, 0, 0);
}

void fill01(buf_elem* buf, size_t size) {
    std_fill2(buf, size, 0, 1);
}

void fill11(buf_elem* buf, size_t size) {
    std_fill2(buf, size, 1, 1);
}

void std_count(buf_elem* buf, size_t size, buf_elem val) {
    auto result = std::count(buf, buf + size, val);
    opt_control::sink(result);
    opt_control::sink_ptr(buf);
}

void count0(buf_elem* buf, size_t size) {
    std_count(buf, size, 0);
}

void count1(buf_elem* buf, size_t size) {
    std_count(buf, size, 1);
}

void std_memcpy(buf_elem* buf, size_t size) {
    auto half = size / 2;
    memcpy(buf, buf + half, half * sizeof(buf_elem));
    opt_control::sink_ptr(buf);
}

/**
 * Writes only a single int per cache line, testing whether we need 
 * a full overwrite to get the optimization.
 * 
 * Doesn't vectorize (almost certainly).
 */
HEDLEY_NEVER_INLINE
void fill_one_per_cl(buf_elem* buf, size_t size, buf_elem val) {
    for (buf_elem* end = buf + size; buf < end; buf += CACHE_LINE_BYTES / sizeof(buf_elem)) {
        *buf = val;
    }
}

void one_per0(buf_elem* buf, size_t size) {
    fill_one_per_cl(buf, size, 0);
}

void one_per1(buf_elem* buf, size_t size) {
    fill_one_per_cl(buf, size, 1);
}

/**
 * Writes an inner buffer of size N with val0, then overwrites
 * that same region with val1, the repeats this over the next
 * chunk of the outer buffer (size "size").
 */
template <size_t N>
HEDLEY_NEVER_INLINE
void double_pump(buf_elem* buf, size_t size, buf_elem val0, buf_elem val1) {
    for (size_t i = 0; i < size; i += N) {
        auto end = std::min(i + N, size); // last copy will (probably) be shorter
        std::fill(buf + i, buf + end, val0);
        opt_control::sink_ptr(buf);
        std::fill(buf + i, buf + end, val1);
        opt_control::sink_ptr(buf);
    }
}

void dp0(buf_elem* buf, size_t size) {
    double_pump<1024>(buf, size, 0, 0);
}

void dp1(buf_elem* buf, size_t size) {
    double_pump<1024>(buf, size, 1, 0);
}

