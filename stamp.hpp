#ifndef STAMP_HPP_
#define STAMP_HPP_

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>

#include <map>

#include "perf-timer.hpp"
#include "hedley.h"

class StampConfig;

/**
 * Test stamp encapsulates everything we measure before and after the code
 * being benchmarked.
 */
class Stamp {
    friend StampConfig;

public:
    constexpr static size_t  MAX_MSR = 1;

    Stamp() : tsc(-1) {}

    Stamp(uint64_t tsc, event_counts counters, uint64_t tsc_before, size_t retries)
        : tsc{tsc},  tsc_before{tsc_before}, counters{counters}, retries{retries} {}

    std::string to_string() { return std::string("tsc: ") + std::to_string(this->tsc); }

    static void set_verbose(bool v);

    uint64_t tsc, tsc_before;
    event_counts counters;
    size_t retries;
    uint64_t msr_values[MAX_MSR];
    size_t msrs_read;
};

/**
 * Taking the delta of two stamps gives you a stamp delta.
 *
 * The StampData has a reference to the StampConfig from which it was created, so the
 * lifetime of the StampConfig must be at least as long as the StampDelta.
 */
class StampDelta {
    friend Stamp;
    friend StampConfig;

    bool empty;
    const StampConfig* config;
    // not cycles: has arbitrary units
    uint64_t tsc_delta;
    event_counts counters;

    StampDelta(const StampConfig& config,
               uint64_t tsc_delta,
               event_counts counters);

public:
    /**
     * Create an "empty" delta - the only thing an empty delta does is
     * never be returned from functions like min(), unless both arguments
     * are empty. Handy for accumulation patterns.
     */
    StampDelta() : empty(true), config{nullptr}, tsc_delta{}, counters{} {}

    double get_nanos() const;

    uint64_t get_tsc() const;

    event_counts get_counters();

    const StampConfig& get_config();

    uint64_t get_counter(const PerfEvent& event) const;

    /**
     * Return a new StampDelta with every contained element having the minimum
     * value between the left and right arguments.
     *
     * As a special rule, if either argument is empty, the other argument is returned
     * without applying the function, this facilitates typical use with an initial
     * empty object followed by accumulation.
     */
    template <typename F>
    static StampDelta apply(const StampDelta& l, const StampDelta& r, F f) {
        if (l.empty)
            return r;
        if (r.empty)
            return l;
        assert(l.config == r.config);
        event_counts new_counts            = event_counts::apply(l.counters, r.counters, f);
        return StampDelta{*l.config, {f(l.tsc_delta, r.tsc_delta)}, new_counts};
    }

    static StampDelta min(const StampDelta& l, const StampDelta& r);
    static StampDelta max(const StampDelta& l, const StampDelta& r);
};

/**
 * Manages PMU events.
 */
class EventManager {
    /** event to counter index */
    std::map<PerfEvent, size_t> event_map;
    std::vector<PerfEvent> event_vec;
    std::vector<bool> setup_results;
    size_t next_counter;
    bool prepared;

public:
    EventManager() : next_counter(0), prepared(false) {}

    bool add_event(const PerfEvent& event);

    void prepare();

    /**
     * Return the counter slot for the given event, or -1
     * if the event was requested but setting up the counter
     * failed.
     *
     * If an event is requested that was never configured,
     * NonExistentCounter exception is thrown.
     */
    ssize_t get_mapping(const PerfEvent& event) const;

    /** number of unique configured events */
    size_t get_count() {
        return event_map.size();
    }
};


static inline int read_msr_cur_cpu(uint32_t id, uint64_t* value) {
    (void)id; (void)value;
    throw std::logic_error("MSR not supported"); // grab the code from freq-bench if you want it
}

/**
 * Manages PMU events.
 */
class MSRManager {

    std::vector<uint32_t> msrids;

    using result_type = uint64_t[Stamp::MAX_MSR];

    uint64_t read_msr(uint32_t id) const {
        uint64_t value = 0;
        int err = read_msr_cur_cpu(id, &value);
        (void)err;
        assert(err == 0);
        return value;
    }

public:
    MSRManager() {}

    void add_msr(uint32_t id) {
        msrids.push_back(id);
    }

    void prepare() {
        if (msrids.size() > Stamp::MAX_MSR) {
            throw std::runtime_error("number of MSR reads exceeds MAX_MSR"); // just increase MAX_MSR
        }
        // try to read all the configured MSRs, in order to fail fast
        for (auto id : msrids) {
            uint64_t value = 0;
            int err = read_msr_cur_cpu(id, &value);
            if (err) {
                throw std::runtime_error(std::string("MSR ") + std::to_string(id) + " read failed with error " + std::to_string(err));
            }
        }
    }

    HEDLEY_ALWAYS_INLINE
    void do_stamp(Stamp &stamp) const {
        if (HEDLEY_UNLIKELY(!msrids.empty())) {
            do_stamp_slowpath(stamp);
        }
    }

    HEDLEY_NEVER_INLINE
    void do_stamp_slowpath(Stamp &stamp) const {
        size_t i = 0;
        for (auto id : msrids) {
            stamp.msr_values[i++] = read_msr(id);
        }
        stamp.msrs_read = i;
        // dbg(stamp.msrs_read);
    }

    uint64_t get_value(uint32_t id, const Stamp& stamp) const;
};

/**
 * A class that holds configuration for creating stamps.
 *
 * Configured based on what columns are requested, holds configuration for varous types
 * of objects.
 */
class StampConfig {
public:
    EventManager em;
    MSRManager mm;

    StampConfig();

    /**
     * After updating the config to the state you want, call prepare() once which
     * does any global configuration needed to support the configured stamps, such
     * as programming PMU events.
     */
    void prepare();

    // take the stamp.
    Stamp stamp() const;

    /**
     * Create a StampDelta from the given before/after stamps
     * which should have been created by this StampConfig.
     */
    StampDelta delta(const Stamp& before, const Stamp& after) const;
};


#endif // STAMP_HPP_