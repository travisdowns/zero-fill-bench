#include "stamp.hpp"
#include "perf-timer.hpp"
#include "perf-timer-events.hpp"
#include "hedley.h"

#include <algorithm>
#include <assert.h>
#include <map>
#include <stdexcept>

#if USE_RDTSC
#include "tsc-support.hpp"

static inline uint64_t get_timestamp() {
    return rdtsc();
}

static inline uint64_t to_nanos(uint64_t tsc) {
    static auto tsc_freq = get_tsc_freq(false);
    return 1000000000. * tsc / tsc_freq;
}

#else
#include <chrono>

static inline uint64_t get_timestamp() {
    using clk = std::chrono::high_resolution_clock;
    return std::chrono::nanoseconds(clk::now().time_since_epoch()).count();
}

static inline uint64_t to_nanos(uint64_t tsc) {
    return tsc; // already in nanos
}


#endif

#define vprint(...)                       \
    do {                                  \
        if (verbose)                      \
            fprintf(stderr, __VA_ARGS__); \
    } while (false)

static bool verbose;

void set_stamp_verbose(bool v) { verbose = v; }

/**
 * Sometimes it's useful to have a functor object rather than a template
 * function since this can be passed to a function which may apply it to d
 * different object types.
 */
struct min_functor {
    template <typename T>
    const T& operator()(const T& l, const T& r) const { return std::min(l, r); }
};

struct max_functor {
    template <typename T>
    const T& operator()(const T& l, const T& r) const { return std::max(l, r); }
};

/**
 * Thrown when the caller asks for a counter that was never configured.
 */
struct NonExistentCounter : public std::logic_error {
    NonExistentCounter(const PerfEvent& e) : std::logic_error(std::string("counter ") + e.name() + " doesn't exist") {}
};

void Stamp::set_verbose(bool v) { verbose = v; }

double StampDelta::get_nanos() const {
    return to_nanos(get_tsc());
}

uint64_t StampDelta::get_tsc() const {
    assert(!empty);
    return tsc_delta;
}

StampDelta::StampDelta(const StampConfig& config,
               uint64_t tsc_delta,
               event_counts counters)
        : empty(false),
          config{&config},
          tsc_delta{tsc_delta},
          counters{std::move(counters)}
          {}

StampDelta StampDelta::min(const StampDelta& l, const StampDelta& r) { return apply(l, r, min_functor{}); }
StampDelta StampDelta::max(const StampDelta& l, const StampDelta& r) { return apply(l, r, max_functor{}); }


const PerfEvent DUMMY_EVENT_NANOS = PerfEvent("nanos", "nanos");


bool EventManager::add_event(const PerfEvent& event) {
    if (event == NoEvent || event == DUMMY_EVENT_NANOS) {
        return true;
    }
    vprint("Adding event %s\n", to_string(event).c_str());
    prepared = false;
    if (event_map.count(event)) {
        return true;
    }
    if (event_map.size() == MAX_COUNTERS) {
        return false;
    }
    event_map.insert({event, next_counter++});
    event_vec.push_back(event);
    return true;
}

void EventManager::prepare() {
    assert(event_map.size() == event_vec.size());
    setup_results   = setup_counters(event_vec);
    size_t failures = std::count(setup_results.begin(), setup_results.end(), false);
    if (failures > 0) {
        fprintf(stderr, "%zu events failed to be configured\n", failures);
    }
    vprint("EventManager configured %zu events\n", (setup_results.size() - failures));
    prepared = true;
}

ssize_t EventManager::get_mapping(const PerfEvent& event) const {
    if (!prepared) {
        throw std::logic_error("not prepared");
    }
    auto mapit = event_map.find(event);
    if (mapit == event_map.end()) {
        throw NonExistentCounter(event);
    }
    size_t idx = mapit->second;
    // printf("counter %s: idx=%zu res=%d\n", event.name(), idx, setup_results.at(idx));
    if (!setup_results.at(idx)) {
        return -1;
    }
    return idx;
}

uint64_t MSRManager::get_value(uint32_t id, const Stamp& stamp) const {
    auto pos = std::find(msrids.begin(), msrids.end(), id);
    // dbg(msrids.size());
    if (pos == msrids.end()) {
        throw std::logic_error("MSR id not found in list");
    }
    size_t idx = pos - msrids.begin();
    if (idx >= stamp.msrs_read) {
        // dbg(idx);
        // dbg(stamp.msrs_read);
        throw std::logic_error("MSR wasnt read");
    }
    assert(idx < Stamp::MAX_MSR);
    return stamp.msr_values[idx];
}


StampConfig::StampConfig() {}

void StampConfig::prepare() {
    em.prepare();
}

Stamp StampConfig::stamp() const {
    auto tsc_before = get_timestamp();
    auto counters = read_counters();
    auto tsc = get_timestamp();

    Stamp s(tsc, counters, tsc_before, 0);
    mm.do_stamp(s);

    return s;
}

StampDelta StampConfig::delta(const Stamp& before, const Stamp& after) const {
    return StampDelta(*this, after.tsc - before.tsc, calc_delta(before.counters, after.counters));
}

uint64_t StampDelta::get_counter(const PerfEvent& event) const {
    const EventManager& em = config->em;
    ssize_t idx            = em.get_mapping(event);
    assert(idx >= -1 && idx <= (ssize_t)MAX_COUNTERS);
    if (idx == -1) {
        return -1;
    }
    return this->counters.counts[idx];
}