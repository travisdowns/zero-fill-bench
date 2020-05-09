/* simple PMU counting capabilities */
#ifndef PERF_TIMER_H_
#define PERF_TIMER_H_

#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

constexpr size_t MAX_COUNTERS = 8;

class PerfEvent {
    std::string name_;
    std::string event_string_;

public:

    /**
     * Event with only a name, which is used to resolve the specific event
     * to use on the current hardware.
     */
    PerfEvent(const std::string& name) : name_{name} {}

    /**
     * event with a specific event string, which is used to define the event, the name is used for
     * display only.
     */
    PerfEvent(const std::string& name, const std::string& event_string) : name_{name}, event_string_{event_string} {}

    const char* name() const { return name_.c_str(); }

    /**
     * Return the event string: which is either the explicitly specified event string if there
     * is one, or the name otherwise (since the name is presumably a valid event string).
     */
    const char* event_string() const { return (has_event_string() ? event_string_ : name_).c_str(); }

    /**
     * True if this event has a dedicated event string separate from the name.
     */
    bool has_event_string() const { return !event_string_.empty(); }

    bool operator==(const PerfEvent& rhs) const { return std::strcmp(name(), rhs.name()) == 0; }
    bool operator!=(const PerfEvent& rhs) const { return !(*this == rhs); }
    bool operator< (const PerfEvent& rhs) const { return std::strcmp(name(), rhs.name()) < 0; }
};

static inline std::string to_string(const PerfEvent& e) {
    return std::string("event[name=") + e.name() + ",event_string=" + e.event_string() + "]";
}

struct uninit_tag{};

struct event_counts {
    uint64_t counts[MAX_COUNTERS];

    event_counts() : counts{} {}

    event_counts(uninit_tag) {}

    /** apply binary op to every pair of elements in the array, returning a new event_counts */
    template<typename F>
    static event_counts apply(const event_counts& l, const event_counts& r, const F& f) {
        event_counts ret;
        for (size_t i = 0; i < MAX_COUNTERS; i++) {
            ret.counts[i] = f(l.counts[i], r.counts[i]);
        }
        return ret;
    }
};

void list_events();

/**
 * Sets up the PMU to record the given events. Currently doesn't remove
 * any events set up earlier, so the list will keep growing (often you)
 * just set up counters once for the lifetime of the process.
 *
 * Returns a list of indexes, corresponding to the past events: the index
 * is the location in event_counts where you'll find the corresponding
 * counter.
 */
std::vector<bool> setup_counters(const std::vector<PerfEvent>& events);

event_counts read_counters();

/* number of succesfully programmed counters */
size_t num_counters();

void perf_timer_set_verbose(bool v);

/**
 * Calculate the delta between two event sets, up to max_event if specified.
 *
 * The value of counts betweem max_event and MAX_COUNTERS are unspecified.
 */
event_counts calc_delta(event_counts before, event_counts after, size_t max_event = MAX_COUNTERS);

std::vector<PerfEvent> get_all_events();

#endif // #ifndef PERF_TIMER_H_
