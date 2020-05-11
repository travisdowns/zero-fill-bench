
#include "perf-timer.hpp"

#include <assert.h>
#include <err.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <vector>

#include "jevents/jevents.h"
#include "jevents/rdpmc.h"
#include "perf-timer-events.hpp"
#if USE_RDTSC
#include "tsc-support.hpp"
#endif

static bool verbose;
static bool debug; // lots of output

PerfEvent::PerfEvent(const std::string& name, const std::string& event_string)
        : name_{name}, event_string_{event_string} {}

PerfEvent::PerfEvent(const char* name, const char* event_string)
        : PerfEvent{std::string(name), std::string(event_string ? event_string : "")} {}

struct event_ctx {
    enum Mode {
        INVALID,
        RDPMC,
        FILE,
        FAILED
    };

    event_ctx(bool failed = false) : event{NoEvent}, mode{failed ? FAILED : INVALID} {}

    event_ctx(PerfEvent event, struct perf_event_attr attr, struct rdpmc_ctx jevent_ctx) :
            event{event}, attr{attr}, jevent_ctx{jevent_ctx}, mode{RDPMC} {}

    event_ctx(PerfEvent event, struct perf_event_attr attr, int fd, int cpu = -1) :
            event{event}, attr{attr}, fd{fd}, cpu{cpu}, mode{FILE}{}

    // the associated event
    PerfEvent event;
    // the perf_event_attr structure that was used to open the event
    struct perf_event_attr attr;


    // the jevents context object
    struct rdpmc_ctx jevent_ctx;
    int fd;
    int cpu; // the cpu on which this counter is valid

    Mode mode;

    bool is_valid() { return mode == RDPMC || mode == FILE; }

    // a string representing the mode the counter is operating in
    std::string mode_string() {
        switch (mode) {
            case RDPMC:
                return "thread-specific RDPMC";
            case FILE:
                if (cpu == -1) {
                    return "thread-specific fd reads (syscall)";
                } else {
                    return std::string("full-cpu ") + std::to_string(cpu) + " fd reads (syscall)";
                }
            case FAILED:
                return "FAILED";
            case INVALID:
                ; // fallthru
        }
        return "INVALID";
    }

    uint64_t rdpmc_readx();
    uint64_t read_file();

    uint64_t read_counter() {
        assert(is_valid());
        return mode == RDPMC ? rdpmc_readx() : read_file();
    }
};

std::vector<event_ctx> contexts;

/**
 * If true, echo debugging info about the perf timer operation to stderr.
 * Defaults to false.
 */
void perf_timer_set_verbose(bool v) {
    verbose = v;
}

#define vprint(...) do { if (verbose) fprintf(stderr, __VA_ARGS__ ); } while(false)

/**
 * Take a perf_event_attr objects and return a string representation suitable
 * for use as an event for perf, or just for display.
 */
void printf_perf_attr(FILE *f, const struct perf_event_attr* attr) {
    char* pmu = resolve_pmu(attr->type);
    fputs(pmu ? pmu : "???", f);
    bool comma = false;
    fprintf(f, "/");

#define APPEND_IF_NZ1(field) APPEND_IF_NZ2(field,field)
#define APPEND_IF_NZ2(name, field) if (attr->field || true) { \
        fprintf(f, "%s" #name "=0x%lx", comma ? "," : "", (long)attr->field); \
        comma = true; \
    }

    APPEND_IF_NZ1(config);
    APPEND_IF_NZ1(config1);
    APPEND_IF_NZ1(config2);
    APPEND_IF_NZ2(period, sample_period);
    APPEND_IF_NZ1(sample_type);
    APPEND_IF_NZ1(read_format);

    fprintf(f, "/");
}

void print_caps(FILE *f, const struct rdpmc_ctx *ctx) {
    fprintf(f, "R%d UT%d ZT%d index: 0x%x",
        (int)ctx->buf->cap_user_rdpmc, (int)ctx->buf->cap_user_time, (int)ctx->buf->cap_user_time_zero, ctx->buf->index);

#define APPEND_CTX_FIELD(field) fprintf(f, " " #field "=0x%lx", (long unsigned)ctx->buf->field);

    APPEND_CTX_FIELD(pmc_width);
    APPEND_CTX_FIELD(offset);
    APPEND_CTX_FIELD(time_enabled);
    APPEND_CTX_FIELD(time_running);

#if USE_RDTSC
    fprintf(f, " rdtsc=0x%lx", (long unsigned)rdtsc());
#endif
}

/* list the events in markdown format */
void list_events() {
    const char *fmt = "| %-27s |\n";
    printf(fmt, "Name");
    printf(fmt, "-------------------------", "-----------");
    for (auto& e : get_all_events()) {
        printf(fmt, e.name());
    }
}

void print_failure(PerfEvent e, struct perf_event_attr attr, const char *reason) {
    fprintf(stderr, "Failed to program event '%s' (reason: %s, errno: %d). \n\tResolved to: ",
            e.name(), reason, errno);
    printf_perf_attr(stderr, &attr);
    fprintf(stderr, "\n");
}

bool disable_rdpmc() {
    const char *var = getenv("PERF_TIMER_NO_RDPMC");
    return var && strcmp(var, "0");
}

int open_for_rdpmc(struct perf_event_attr *attr, struct rdpmc_ctx *ctx) {
    if (disable_rdpmc()) {
        return -1;
    }
	ctx->fd = perf_event_open(attr, 0, -1, -1, 0);
	if (ctx->fd < 0) {
		return ctx->fd;
	}
	ctx->buf = (perf_event_mmap_page *)mmap(NULL, sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, ctx->fd, 0);
	if (ctx->buf == MAP_FAILED) {
		close(ctx->fd);
		return -1;
	}
    if (!ctx->buf->cap_user_rdpmc) {
        return -1;
    }
    return 0;
}

bool make_rdpmc_event(PerfEvent e, perf_event_attr attr, event_ctx& event_context, bool printfail) {
    struct rdpmc_ctx ctx = {};
    int ret;
    if ((ret = open_for_rdpmc(&attr, &ctx)) || ctx.buf->index == 0) {
        if (printfail) {
            print_failure(e, attr, ret ? "rdpmc_open_attr failed" : "no index, probably too many or incompatible events");
        }
        return false;
    }
    event_context = {e, attr, ctx};
    return true;
}

bool make_read_event(PerfEvent e, perf_event_attr attr, event_ctx& event_context, bool wholecpu, bool printfail) {
    attr.pinned = 0;
    int pid, cpu;
    if (wholecpu) {
        pid = -1;
        cpu = sched_getcpu();
    } else {
        pid = 0;
        cpu = -1;
    }
    int fd = perf_event_open(&attr, pid, cpu, -1, 0);
    if (verbose) printf_perf_attr(stderr, &attr);
    if (fd < 0) {
        if (printfail) {
            print_failure(e, attr, "perf_event_open failed");
        }
        return false;
    }
    event_context = {e, attr, fd, cpu};
    return true;
}

std::vector<bool> setup_counters(const std::vector<PerfEvent>& events) {

    std::vector<bool> results;

    for (auto& e : events) {
        bool ok = false;

        if (contexts.size() == MAX_COUNTERS) {
            fprintf(stderr, "Unable to program event %s, MAX_COUNTERS (%zu) reached\n", e.name(), MAX_COUNTERS);
        } else {
            // fprintf(stderr, "Enabling event %s (%s)\n", e->short_name, e->name);
            struct perf_event_attr attr{};
            int err = -1;
            jevent_extra jextra{};

            if (e.has_event_string()) {
                // can resolve perf style events like cpu/config=123/ so try it first
                // since resolve_event_extra fails to do that if the json files 
                // don't exist
                err = jevent_name_to_attr_extra(e.event_string(), &attr, &jextra);
            }

            if (err) {
                err = resolve_event_extra(e.event_string(), &attr, &jextra);
            }
            
            if (verbose && !err) fprintf(stderr, ">> %s (mpmu: %d): %s\n", e.name(), jextra.multi_pmu, jextra.decoded);

            if (err) {
                fprintf(stderr, "Unable to resolve event '%s' - report this as a bug along with your CPU model string\n", e.name());
                fprintf(stderr, "jevents error %2d: %s\n", err, jevent_error_to_string(err));
                fprintf(stderr, "jevents details : %s\n", jevent_get_error_details());
            } else if (jextra.multi_pmu) {
                fprintf(stderr, "Event '%s' failed as it needs multi-pmu (not supported)\n", e.name());
            } else {
                attr.sample_period = 0;
                // pinned makes the counter stay on the CPU and fail fast if it can't be allocated: we
                // can check right away if index == 0 which means failure
                attr.pinned = 1;
                attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;

                event_ctx ctx;
                // prefer rdpmc style events
                // ... but if it fails, fall back to read(2) style events
                // first trying the process-specific version, and then
                // the whole cpu version (which works for some events
                // like uncore that can't count in a process-specific way)
                ok = make_rdpmc_event(e, attr, ctx, false) || 
                        make_read_event(e, attr, ctx, false, false) ||
                        make_read_event(e, attr, ctx, true, true);

                if (ok) {
                    contexts.push_back(ctx);
                }
             }
        }
        if (!ok) {
            contexts.push_back(event_ctx{true}); // dummy failed event
        }
        results.push_back(ok);
    }

    // output all the event details after all have been programmed since later events with constaints might
    // change the index for earlier ones
    if (verbose) {
        for (size_t i = 0; i < contexts.size(); i++) {
            event_ctx ec = contexts[i];
            if (ec.is_valid()) {
                vprint("Resolved and programmed event '%s' to ", ec.event.name());
                printf_perf_attr(stderr, &ec.attr);
                vprint("\n    mode: %s", ec.mode_string().c_str());
                if (ec.mode == event_ctx::RDPMC) {
                    vprint("\n    caps: ");
                    print_caps(stderr, &ec.jevent_ctx);
                }
                vprint("\n");
            }
        }
    }

    assert(results.size() == events.size());
    assert(contexts.size() == events.size());
    return results;
}

uint64_t event_ctx::read_file() {
    assert(mode == FILE);

    struct record {
        uint64_t value;
        uint64_t time_enabled;
        uint64_t time_running;
    };

    record r{};

    int ret = read(fd, &r, sizeof(r));
    if (ret != sizeof(r)) {
        fprintf(stderr, "read(2) failed: %d %d\n", ret, errno);
        return 0;
    } else {
        // printf("READ: %zu %zu %zu\n", r.value, r.time_running, r.time_enabled);
        if (r.time_enabled == r.time_running) {
            return r.value;
        } else {
            return (double)r.value * r.time_enabled / r.time_running;
        }
    }
}

/**
 * rdpmc_read - read a ring 3 readable performance counter
 * @ctx: Pointer to initialized &rdpmc_ctx structure.
 *
 * Read the current value of a running performance counter.
 * This should only be called from the same thread/process as opened
 * the context. For new threads please create a new context.
 */
uint64_t event_ctx::rdpmc_readx()
{
    static_assert(sizeof(uint64_t) == sizeof(unsigned long long));
    assert(mode == event_ctx::RDPMC);
    typedef uint64_t u64;
#define rmb() asm volatile("" ::: "memory")

	u64 val;
	unsigned seq;
	u64 offset, time_running, time_enabled;
	struct perf_event_mmap_page *buf = jevent_ctx.buf;
	unsigned index;
    bool lockok = true;

	do {
		seq = buf->lock;
		rmb();
		index = buf->index;
		offset = buf->offset;
        time_enabled = buf->time_enabled;
        time_running = buf->time_running;
		if (index == 0) { /* rdpmc not allowed */
            val = 0;
            rmb();
            lockok = (buf->lock == seq);
			break;
        }
#if defined(__ICC) || defined(__INTEL_COMPILER)
		val = _rdpmc(index - 1);
#else
		val = __builtin_ia32_rdpmc(index - 1);
#endif
		rmb();
	} while (buf->lock != seq);

    u64 res  = val + offset;
    u64 res2 = (res << (64 - buf->pmc_width)) >> (64 - buf->pmc_width);

    if (debug) {
        vprint("read counter %-30s ", event.name());
#define APPEND_LOCAL(local, fmt) fprintf(stderr, " " #local "=0x%" #fmt "lx", (long unsigned)local);
        APPEND_LOCAL(lockok, 1);
        APPEND_LOCAL(val, 013);
        APPEND_LOCAL(offset, 013);
        // APPEND_LOCAL(res, 013);
        APPEND_LOCAL(res2, 012);
        APPEND_LOCAL(time_enabled, 08);
        APPEND_LOCAL(time_running, 08);
        APPEND_LOCAL(index, );
        vprint("\n");
    }
    return res2;
}


event_counts read_counters() {
    event_counts ret{uninit_tag{}};
    for (size_t i = 0; i < contexts.size(); i++) {
        auto& ctx = contexts[i];
        assert(ctx.mode != event_ctx::INVALID);
        if (ctx.is_valid()) {
            assert(i < MAX_COUNTERS);
            ret.counts[i] = ctx.read_counter();
        }
    }
    return ret;
}

size_t num_counters() {
    return contexts.size();
}

event_counts calc_delta(event_counts before, event_counts after, size_t max_event) {
    event_counts ret(uninit_tag{});
    size_t limit = std::min(max_event, MAX_COUNTERS);
    for (size_t i=0; i < limit; i++) {
        ret.counts[i] = after.counts[i] - before.counts[i];
    }
    return ret;
}
