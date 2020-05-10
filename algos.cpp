#include "common.hpp"
#include "hedley.h"
#include "opt-control.hpp"

#include <algorithm>
#include <string.h>

#ifdef __AVX__
#include <immintrin.h>
#endif

void memset_val(buf_elem* buf, size_t bufsz, int c) {
    memset(buf, c, bufsz * sizeof(buf[0]));
    opt_control::sink_ptr(buf);
}

void memset0(buf_elem* buf, size_t bufsz) {
    memset_val(buf, bufsz, 0);
}

void memset1(buf_elem* buf, size_t bufsz) {
    memset_val(buf, bufsz, 1);
}

HEDLEY_NEVER_INLINE
void std_fill(buf_elem* buf, size_t bufsz, buf_elem val) {
    std::fill(buf, buf + bufsz, val);
    opt_control::sink_ptr(buf);
}

void fill0(buf_elem* buf, size_t bufsz) {
    std_fill(buf, bufsz, 0);
}

void fill1(buf_elem* buf, size_t bufsz) {
    std_fill(buf, bufsz, 1);
}

void filln1(buf_elem* buf, size_t bufsz) {
    std_fill(buf, bufsz, -1);
}

HEDLEY_NEVER_INLINE
void avx_fill(buf_elem* buf, size_t bufsz, buf_elem val0, buf_elem val1) {
#ifdef __AVX__

    auto vbuf = (__m256i *)buf;
    __m256i filler0 = _mm256_set1_epi32(val0);
    __m256i filler1 = _mm256_set1_epi32(val1);
    size_t chunks = (bufsz * sizeof(buf_elem) + 31) / 32;
    for (size_t c = 0; c < chunks; c += 4) {
        _mm256_store_si256(vbuf + c + 0, filler0);
        _mm256_store_si256(vbuf + c + 1, filler0);
        _mm256_store_si256(vbuf + c + 2, filler1);
        _mm256_store_si256(vbuf + c + 3, filler1);
    }    
    opt_control::sink_ptr(vbuf);
#else
    assert(false);
#endif
}

void avx0(buf_elem* buf, size_t bufsz) {
    avx_fill(buf, bufsz, 0, 0);
}

void avx1(buf_elem* buf, size_t bufsz) {
    avx_fill(buf, bufsz, 1, 1);
}

void avx01(buf_elem* buf, size_t bufsz) {
    avx_fill(buf, bufsz, 0, 1);
}

void std_fill2(buf_elem* buf, size_t bufsz, buf_elem val0, buf_elem val1) {
    std_fill(buf, bufsz, val0);
    std_fill(buf, bufsz, val1);
}

void fill00(buf_elem* buf, size_t bufsz) {
    std_fill2(buf, bufsz, 0, 0);
}

void fill01(buf_elem* buf, size_t bufsz) {
    std_fill2(buf, bufsz, 0, 1);
}

void fill11(buf_elem* buf, size_t bufsz) {
    std_fill2(buf, bufsz, 1, 1);
}

static void std_count(buf_elem* buf, size_t bufsz, buf_elem val) {
    auto result = std::count(buf, buf + bufsz, val);
    opt_control::sink(result);
    opt_control::sink_ptr(buf);
}

void count0(buf_elem* buf, size_t bufsz) {
    std_count(buf, bufsz, 0);
}

void count1(buf_elem* buf, size_t bufsz) {
    std_count(buf, bufsz, 1);
}

void std_memcpy(buf_elem* buf, size_t bufsz) {
    auto half = bufsz / 2;
    memcpy(buf, buf + half, half * sizeof(buf_elem));
    opt_control::sink_ptr(buf);
}