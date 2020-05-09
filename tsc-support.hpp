/*
 * tsc-support.cpp
 */

#include <cinttypes>
#include <string>

#ifdef _MSC_VER
#include <intrin.h>
#elif defined(TSC_SUPPORT_USE_INTRINSIC)
#include <x86intrin.h>
#endif

static inline uint64_t rdtsc() {
#ifdef TSC_SUPPORT_USE_INTRINSIC
    return __rdtsc();
#else
    // https://stackoverflow.com/a/13772771
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
#endif
}

/**
 * Get the TSC frequency.
 *
 * By default, this tries to read the TSC frequency directly from cpuid leaf 0x15,
 * if it is on a supported architecture, otherwise it falls back to using a calibration
 * loop. If force_calibrate is true, it always uses the calibration loop and never reads
 * from cpuid.
 */
std::uint64_t get_tsc_freq(bool force_calibrate);

/** return a string describing how the TSC frequency was determined */
const char* get_tsc_cal_info(bool force_calibrate);

/** set the file to log messages to, or nullptr if messages should simply be dropped */
void set_logging_file(FILE *file);
