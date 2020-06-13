#include "common.hpp"
#include "hedley.h"
#include "opt-control.hpp"

#include <algorithm>
#include <assert.h>
#include <string.h>

#ifdef __AVX__
#include <immintrin.h>
#endif


#define DELEGATE_ONE(fn, deleg, v) \
    void fn(buf_elem* buf, size_t size) { \
        deleg(buf, size, v); \
    } \

/**
 * Given a prefix and deleg, creates functions
 * prefix0 and prefix1 calling delegate with 0 and 1.
 */
#define DELEGATE_01(prefix, deleg) DELEGATE_ONE(prefix##0, deleg, 0) DELEGATE_ONE(prefix##1, deleg, 1)

constexpr size_t CL_SIZE = 64; // assumed size of a cache line

void memset_val(buf_elem* buf, size_t size, int c) {
    memset(buf, c, size * sizeof(buf[0]));
    opt_control::sink_ptr(buf);
}

DELEGATE_01(memset, memset_val)

HEDLEY_NEVER_INLINE
void std_fill(buf_elem* buf, size_t size, buf_elem val) {
    std::fill(buf, buf + size, val);
    opt_control::sink_ptr(buf);
}

DELEGATE_01(fill, std_fill);
DELEGATE_ONE(filln1, std_fill, -1);

HEDLEY_NEVER_INLINE
void avx_fill_alt(buf_elem* buf, size_t size, buf_elem val0, buf_elem val1) {
#ifdef __AVX__
    static_assert(BUFFER_TAIL_BYTES >= 127, "buffer tail too small");
    auto vbuf = (__m256i *)buf;
    __m256i filler0 = _mm256_set1_epi32(val0);
    __m256i filler1 = _mm256_set1_epi32(val1);
    size_t chunks = (size * sizeof(buf_elem) + 31) / 32;
    // we may overwrite by up to 127 bytes (one full iteration of writes - 1)
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
    avx_fill_alt(buf, size, 0, 0);
}

void avx1(buf_elem* buf, size_t size) {
    avx_fill_alt(buf, size, 1, 1);
}

void avx01(buf_elem* buf, size_t size) {
    avx_fill_alt(buf, size, 0, 1);
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

DELEGATE_01(count, std_count);

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

DELEGATE_01(one_per, fill_one_per_cl)


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

constexpr size_t DP_SIZE = 16 * 1024; // 16k * 4 bytes = 64 KiB
// constexpr size_t DP_SIZE = 512 * 1024 / 4; // 512 KiB

void dp00(buf_elem* buf, size_t size) {
    double_pump<DP_SIZE>(buf, size, 0, 0);
}

void dp10(buf_elem* buf, size_t size) {
    double_pump<DP_SIZE>(buf, size, 1, 0);
}

void dp11(buf_elem* buf, size_t size) {
    double_pump<DP_SIZE>(buf, size, 1, 1);
}

// 0x0101010101010101

#ifdef __AVX__

static_assert(BUFFER_TAIL_BYTES >= 63, "buffer tail too small");

HEDLEY_NEVER_INLINE
void fill64(buf_elem* buf, size_t size, buf_elem val) {
    auto vbuf = (uint64_t *)buf;
    uint64_t vecval = ((uint64_t)val << 32) | val;
    size_t chunks = (size * sizeof(buf_elem) + 7) / 8;
    for (size_t c = 0; c < chunks; c += 8) {
        memcpy(vbuf + c + 0, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 1, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 2, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 3, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 4, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 5, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 6, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
        memcpy(vbuf + c + 7, &vecval, sizeof(vecval));
        opt_control::sink_ptr(buf);
    }
}

DELEGATE_01(fill64_, fill64);

HEDLEY_NEVER_INLINE
void avx_fill128(buf_elem* buf, size_t size, buf_elem val) {
    auto vbuf = (__m128i *)buf;
    __m128i vecval = _mm_set1_epi32(val);
    size_t chunks = (size * sizeof(buf_elem) + 15) / 16;
    for (size_t c = 0; c < chunks; c += 4) {
        _mm_store_si128(vbuf + c + 0, vecval);
        _mm_store_si128(vbuf + c + 1, vecval);
        _mm_store_si128(vbuf + c + 2, vecval);
        _mm_store_si128(vbuf + c + 3, vecval);
    }
}

DELEGATE_01(fill128_, avx_fill128);

void avx_fill256(buf_elem* buf, size_t size, buf_elem val) {
    auto vbuf = (__m256i *)buf;
    __m256i vecval = _mm256_set1_epi32(val);
    size_t chunks = (size * sizeof(buf_elem) + 31) / 32;
    for (size_t c = 0; c < chunks; c += 2) {
        _mm256_store_si256(vbuf + c + 0, vecval);
        _mm256_store_si256(vbuf + c + 1, vecval);
    }
}

DELEGATE_01(fill256_, avx_fill256);
#endif

#ifdef __AVX512F__
HEDLEY_NEVER_INLINE
void avx_fill512(buf_elem* buf, size_t size, buf_elem val) {
    auto vbuf = (__m512i *)buf;
    __m512i vecval = _mm512_set1_epi32(val);
    size_t chunks = (size * sizeof(buf_elem) + 63) / 64;
    for (size_t c = 0; c < chunks; c++) {
        _mm512_store_si512(vbuf + c, vecval);
    }
}

DELEGATE_01(fill512_, avx_fill512);
#endif