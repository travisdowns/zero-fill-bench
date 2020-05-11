#ifndef USE_RDTSC_H_
#define USE_RDTSC_H_

/**
 * Included if USE_RDTSC is 1.
 */

#include <inttypes.h>

#include "tsc-support.hpp"


struct RdtscClock {
    using now_t   = uint64_t;
    using delta_t = uint64_t;

    static now_t now() {
         __builtin_ia32_lfence();
        now_t ret = rdtsc();
         __builtin_ia32_lfence();
        return ret;
    }

    /* accept the result of subtraction of durations and convert to nanos */
    static uint64_t to_nanos(delta_t diff) {
        static double tsc_to_nanos = 1000000000.0 / tsc_freq();
        return diff * tsc_to_nanos;
    }

    static uint64_t now_to_nanos(now_t n) {
        return to_nanos(n);
    }

    static uint64_t tsc_freq() {
        static uint64_t freq = get_tsc_freq(false);
        return freq;
    }

};

#endif