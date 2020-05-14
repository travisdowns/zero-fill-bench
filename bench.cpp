/*
 * bench.cpp
 */

#include <err.h>
#include <error.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "args-wrap.hpp"
#include "common.hpp"
#include "fmt/format.h"
#include "hedley.h"
#include "stamp.hpp"
#include "stats.hpp"
#include "table.hpp"
#include "util.hpp"

#if USE_RDTSC
#include "use-rdtsc.hpp"
#define DefaultClock RdtscClock
#else
#define DefaultClock StdClock<std::chrono::high_resolution_clock>
#endif

using namespace std::chrono;
using namespace Stats;

using std::uint64_t;

struct test_func {
    // function pointer to the test function
    cal_f* func;
    const char* id;
    buf_elem intial; // fill the buffer with this initial value
    double work_factor = 1.;
};

std::vector<test_func> ALL_FUNCS = {
    { memset0     , "memset0"  , 0      },
    { memset1     , "memset1"  , 1      },
    { fill0       , "fill0"    , 0      },
    { fill1       , "fill1"    , 1      },
    { filln1      , "filln1"   ,-1      },
    { avx0        , "alt0"     , 0      },
    { avx1        , "alt1"     , 1      },
    { avx01       , "alt01"    , 0      },
    { fill00      , "fill00"   , 0 , 2. },
    { fill01      , "fill01"   , 0 , 2. },
    { fill11      , "fill11"   , 1 , 2. },
    { count0      , "count0"   , 0      },
    { count1      , "count1"   , 1      },
    { one_per0    , "one_per0" , 0      },
    { one_per1    , "one_per1" , 1      },
    { std_memcpy  , "memcpy"   , 0      },
};

void pin_to_cpu(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
        error(EXIT_FAILURE, errno, "could not pin to CPU %d", cpu);
    }
}

static argsw::ArgumentParser parser{"conc-bench: Demonstrate concurrency perforamnce levels"};

static argsw::HelpFlag help{parser, "help", "Display this help menu", {"help"}};

static argsw::Flag arg_force_tsc_cal{parser, "force-tsc-calibrate",
    "Force manual TSC calibration loop, even if cpuid TSC Hz is available", {"force-tsc-calibrate"}};
static argsw::Flag arg_no_pin{parser, "no-pin",
    "Don't try to pin threads to CPU - gives worse results but works around affinity issues on TravisCI", {"no-pin"}};
static argsw::Flag arg_verbose{parser, "verbose", "Output more info", {"verbose"}};
static argsw::Flag arg_list{parser, "list", "List the available tests and their descriptions", {"list"}};
static argsw::Flag arg_csv{parser, "", "Output a csv table instead of the default", {"csv"}};

static argsw::ValueFlag<std::string> arg_algos{parser, "ALGO1,ALGO2,...", "Run only the algorithms in the comma separated list", {"algos"}};
static argsw::ValueFlag<std::string> arg_perfcols{parser, "COL1,COL2,...", "Include the additional perf-event based columns", {"perf-cols"}};
static argsw::ValueFlag<std::string> arg_perfextra{parser, "EVENT1,EVENT2,...", "Include the additional arbitrary perf events", {"perf-extra"}};
static argsw::ValueFlag<size_t> arg_target_size{parser, "SIZE", "Target size in bytes for each trial, used to calculate internal iters", {"trial-size"}, 100000};
static argsw::ValueFlag<size_t> arg_min_iters{parser, "ITERS", "Minimum number of internal iteratoins for each trial (default 2)", {"min-iters"}, 2};
static argsw::ValueFlag<uint64_t> arg_warm_ms{parser, "MILLISECONDS", "Warmup milliseconds for each thread after pinning (default 100)", {"warmup-ms"}, 100};

static argsw::ValueFlag<size_t> arg_buf_min{parser, "KILOBYTES", "Minimum buffer size in bytes", {"min-size"}, 100};
static argsw::ValueFlag<size_t> arg_buf_max{parser, "KILOBYTES", "Maximum buffer size in bytes", {"max-size"}, 100 * 1000 * 1000};
static argsw::ValueFlag<size_t> arg_buf_sz {parser, "KILOBYTES", "Buffer size (overrides min and max)", {"size"}};
static argsw::ValueFlag<double> arg_step   {parser, "RATIO", "Possibly factional ratio between successive sizes", {"step"}, 4. / 3.};

static bool verbose; // true for verbose output
static FILE* out;    // where non-data (informational) output should go


template <typename CHRONO_CLOCK>
struct StdClock {
    using now_t   = decltype(CHRONO_CLOCK::now());
    using delta_t = typename CHRONO_CLOCK::duration;

    static now_t now() {
        return CHRONO_CLOCK::now();
    }

    /* accept the result of subtraction of durations and convert to nanos */
    static uint64_t to_nanos(delta_t d) {
        return duration_cast<std::chrono::nanoseconds>(d).count();
    }

    static uint64_t now_to_nanos(now_t tp) {
        return to_nanos(tp.time_since_epoch());
    }
};

template <typename CLOCK = DefaultClock>
static uint64_t now_nanos() {
    return CLOCK::now_to_nanos(CLOCK::now());
}

/*
 * The result of the run_test method, with only the stuff
 * that can be calculated from within that method.
 */
struct result {
    size_t trial;
    StampDelta delta;
    size_t iters;
    size_t bufsz;

    size_t buf_bytes() const {
        return bufsz * sizeof(buf_elem);
    };
};

struct test_spec {
    test_func func;
    size_t iters;
    int* buf;
    size_t bufsz;
};

struct result_holder {
    test_spec spec;
    size_t iters;

    /** results */
    DescriptiveStats elapsedns_stats;
    uint64_t timed_iters = 0; // the number of iterationreac the ctimed part of the test
    uint64_t total_iters = 0;

    std::vector<result> results;  // the results from each non-warmup trial

    result_holder(test_spec spec, size_t iters) :
            spec{std::move(spec)},
            iters{iters}             
            {}

    template <typename E>
    double inner_sum(E e) const {
        double a = 0;
        for (const auto& result : results) {
            a += e(result);
        }
        return a;
    }

    typedef uint64_t (result_holder::*rh_u64);
    typedef uint64_t (result::*ir_u64);

    double inner_sum(result_holder::ir_u64 pmem) const {
        return inner_sum(std::mem_fn(pmem));
    }
};

struct warmup {
    uint64_t millis;
    warmup(uint64_t millis) : millis{millis} {}

    long warm() {
        auto start = now_nanos();
        long iters = 0;
        while (now_nanos() < 1000000u * millis) {
            iters++;
        }
        return iters;
    }
};

template <typename CLOCK = DefaultClock, size_t MEASURED = 17, size_t WARMUP = 10>
result_holder run_test(const test_spec& spec, const StampConfig& config) {
    constexpr size_t TRIALS = WARMUP + MEASURED;
    const auto iters = spec.iters;

    warmup w{arg_warm_ms.Get()};
    long warms = w.warm();
    if (verbose) {
        fmt::print(out, "Running: id={}, iters={}, bufsz={}, warms={}\n",
                spec.func.id, spec.iters, spec.bufsz, warms);
    }

    // set the buffer to the specified fill value
    std::fill(spec.buf, spec.buf + spec.bufsz, spec.func.intial);

    std::array<typename CLOCK::delta_t, TRIALS> trial_timings;
    std::array<Stamp, TRIALS> trial_stamps;

    result_holder rh(spec, iters);

    for (size_t t = 0; t < TRIALS; t++) {
        auto i = iters;
        auto t0 = CLOCK::now();
        for (size_t i = 0; i < iters; i++) {
            spec.func.func(spec.buf, spec.bufsz);
        }
        auto t1 = CLOCK::now();
        trial_stamps[t] = config.stamp();
        trial_timings[t] = t1 - t0;
    }

    rh.timed_iters = MEASURED * iters;
    rh.total_iters = TRIALS * iters;

    std::array<uint64_t, MEASURED> nanos = {};
    std::transform(trial_timings.begin() + WARMUP, trial_timings.end(), nanos.begin(), CLOCK::to_nanos);
    rh.elapsedns_stats = get_stats(nanos.begin(), nanos.end());

    rh.results.reserve(MEASURED);
    assert(WARMUP > 0);
    for (size_t t = WARMUP; t < TRIALS; t++) {
        auto sd = config.delta(trial_stamps.at(t - 1), trial_stamps.at(t));
        result r{t - WARMUP, sd, iters, spec.bufsz};
        rh.results.push_back(r);
    }
    assert(rh.results.size() == MEASURED);

    return rh;
}

struct usage_error : public std::runtime_error {
    using runtime_error::runtime_error;
};


template <typename E>
double aggregate_results(const std::vector<result>& results, E e) {
    double a = 0;
    for (const auto& result : results) {
        a += e(result);
    }
    return a;
}

using Row = table::Row;
auto LEFT = table::ColInfo::LEFT;
auto RIGHT = table::ColInfo::RIGHT;

struct column_base {
    std::string heading;
    table::ColInfo::Justify j;

    column_base(const std::string& heading, table::ColInfo::Justify j) : heading{heading}, j{j} {}

    virtual void update_config(StampConfig&) const {}

    virtual void add_to_row(Row& row, const result_holder& holder, const result& result) const = 0;
};

using rh_extractor = std::function<void(Row& row, const result_holder& holder)>;

struct rh_column : column_base {
    rh_extractor e;

    rh_column(const char *heading, table::ColInfo::Justify j, rh_extractor e) : column_base{heading, j}, e{e} {}

    virtual void add_to_row(Row& row, const result_holder& holder, const result&) const override {
        e(row, holder);
    }
};

rh_column make_inner(const char* name, result_holder::rh_u64 pmem, const char* format = "%.1f") {
    return rh_column{name, RIGHT,
            [=](Row& r, const result_holder& h) {
                r.addf(format, (double)(h.*pmem));
            }
    };
}


static rh_column col_id  {"Algo",   LEFT, [](Row& r, const result_holder& h){ r.add(h.spec.func.id); }};
static rh_column col_ns  {"Nanos", RIGHT, [](Row& r, const result_holder& h) {
    r.addf("%.1f", h.elapsedns_stats.getMedian() / h.iters); }};
static rh_column col_iter{"Iters", RIGHT, [](Row& r, const result_holder& h){ r.add(h.spec.iters); }};


using delta_extractor = std::function<void(Row& row, const result_holder&, const result&)>;

struct delta_column : column_base {
    delta_extractor e;

    delta_column(const char *heading, table::ColInfo::Justify j, delta_extractor e) : column_base{heading, j}, e{e} {}

    virtual void add_to_row(Row& row, const result_holder& rh, const result& result) const override {
        e(row, rh, result);
    }
};

static delta_column col_size   {"Size",    RIGHT, [](Row& row, const result_holder&, const result& res){ row.add(res.buf_bytes()); }};
static delta_column col_trial  {"Trial",   RIGHT, [](Row& row, const result_holder&, const result& res){ row.add(res.trial); }};
static delta_column col_stampns{"Stampms", RIGHT, [](Row& row, const result_holder&, const result& res){
    row.addf("%0.2f", res.delta.get_nanos() / (1000000. * res.iters));
}};
static delta_column col_gbs{"GB/s",    RIGHT, [](Row& row, const result_holder& rh, const result& res){
    row.addf("%.1f", (double)res.iters * rh.spec.func.work_factor * res.buf_bytes() / res.delta.get_nanos());
}};

enum NormStyle {
    NONE,
    PER_CL
};

static PerfEvent NoEvent{""};

class event_column : public column_base {
    struct Failed {}; // to throw
public:
    std::string format;
    PerfEvent top, bottom;
    NormStyle norm;

    event_column(const std::string& heading, const std::string& format, PerfEvent top, NormStyle norm = NONE)
        : column_base{heading, RIGHT}, format{format}, top{top}, bottom{NoEvent}, norm{norm} {}

    virtual void add_to_row(Row& row, const result_holder& rh, const result& result) const {
        try {
            auto v = get_value(result.delta);
            switch (norm) {
                case PER_CL:
                    v /= (result.buf_bytes() * rh.spec.func.work_factor * result.iters / CACHE_LINE_BYTES);
                    break;
                case NONE:;
            };

            row.addf(format.c_str(), v);
        } catch (const Failed&) {
            row.add("FAIL");
        }
    }

    double get_value(const StampDelta& delta) const {
        return value(delta, top) / (is_ratio() ? value(delta, bottom) : 1.);
    }

    void update_config(StampConfig& sc) const override {
        sc.em.add_event(top);
        sc.em.add_event(bottom);
    }

    /** true if this value is a ratio (no need to normalize), false otherwise */
    bool is_ratio() const {
        return bottom != NoEvent;  // lol
    }

private:
    double value(const StampDelta& delta, const PerfEvent& e) const {
        auto v = delta.get_counter(e);
        if (v == -1ull) {
            throw Failed{};
        }
        return v;
    }
};

using collist = std::vector<const column_base *>;

auto basic_cols = collist{&col_size, &col_id, &col_trial, &col_stampns, &col_gbs, &col_iter};

const PerfEvent UNC_READS("unc_arb_trk_requests.drd_direct");
const PerfEvent UNC_WRITES("unc_arb_trk_requests.writes");
const PerfEvent IMC_READS("uncore_imc/data_reads/");
const PerfEvent IMC_WRITES("uncore_imc/data_writes/");
const PerfEvent L2_OUT_SILENT("l2_lines_out.silent");
const PerfEvent L2_OUT_NON_SILENT("l2_lines_out.non_silent");

const std::vector<event_column> perf_cols = {
    {"Instructions", "%.2f", PerfEvent{"instructions"}, PER_CL},
    {"uncR", "%.2f", UNC_READS, PER_CL},
    {"uncW", "%.2f", UNC_WRITES, PER_CL},
    {"imcR", "%.2f", IMC_READS, PER_CL},
    {"imcW", "%.2f", IMC_WRITES, PER_CL},
    {"l2-out-silent",     "%.2f", L2_OUT_SILENT,     PER_CL},
    {"l2-out-non-silent", "%.2f", L2_OUT_NON_SILENT, PER_CL},
};

void report_results(const collist cols, const std::vector<result_holder>& results_list) {

    // report
    table::Table table;
    table.setColColumnSeparator(" | ");
    auto &header = table.newRow();

    for (size_t c = 0; c < cols.size(); c++) {
        auto& col = *cols[c];
        header.add(col.heading);
        table.colInfo(c).justify = col.j;
    }

    for (const result_holder& holder : results_list) {
        for (const auto& res : holder.results) {
            auto& row = table.newRow();
            for (auto& c : cols) {
                c->add_to_row(row, holder, res);
            }
        }
    }

    printf("%s", (arg_csv ? table.csv_str() : table.str()).c_str());
}

void list_tests() {
    table::Table table;
    table.newRow().add("Algo");
    for (auto& t : ALL_FUNCS) {
        table.newRow().add(t.id);
    }
    printf("Available tests:\n\n%s\n", table.str().c_str());
}

std::vector<int> get_cpus() {
    cpu_set_t cpu_set;
    if (sched_getaffinity(0, sizeof(cpu_set), &cpu_set)) {
        err(EXIT_FAILURE, "failed while getting cpu affinity");
    }
    std::vector<int> ret;
    for (int cpu = 0; cpu < CPU_SETSIZE; cpu++) {
        if (CPU_ISSET(cpu, &cpu_set)) {
            ret.push_back(cpu);
        }
    }
    return ret;
}


int main(int argc, char** argv) {

    // parser.ParseCLI(argc, argv);
    parser.ParseCLI(argc, argv,
        [](const std::string& help) {
            printf("%s\n", help.c_str());
            exit(EXIT_SUCCESS);
        }, [](const std::string& parse_error) {
            fmt::print(stderr, "ERROR while parsing arguments: {}\n", parse_error);
            fmt::print(stderr, "\nUsage:\n{}\n", parser.Help());
            exit(EXIT_FAILURE);
        }   
    );

    verbose = arg_verbose;

    if (verbose) {
        Stamp::set_verbose(true);
        perf_timer_set_verbose(true);
    }

    // if csv mode is on, only the table should go to stdout
    // the rest goes to stderr
    out = arg_csv ? stderr : stdout;

    if (arg_list) {
        list_tests();
        exit(EXIT_SUCCESS);
    }

#if USE_RDTSC
    set_logging_file(out);
    get_tsc_freq(arg_force_tsc_cal);
#endif

    bool is_root = (geteuid() == 0);
    auto minsz = arg_buf_min.Get(), maxsz = arg_buf_max.Get();
    if (arg_buf_sz) {
        minsz = maxsz = arg_buf_sz.Get();
    }

    std::vector<int> cpus = get_cpus();
    if (!arg_no_pin) {
        pin_to_cpu(0);
    }

    auto cols = basic_cols;
    if (arg_perfcols) {
        for (auto& pname : split(arg_perfcols.Get(), ",")) {
            auto itr = std::find_if(perf_cols.begin(), perf_cols.end(), [pname](event_column ec){ return pname == ec.heading; });
            if (itr == perf_cols.end()) {
                fmt::print("perf column {} not found\n", pname);
                exit(EXIT_FAILURE);
            } else {
                cols.push_back(&*itr);
            }
        }
    }

    if (arg_perfextra) {
        for (auto& eventstr : split(arg_perfextra.Get(), ",")) {
            fmt::print(out, "adding extra event: {}\n", eventstr);
            event_column* col = new event_column(eventstr, "%.2f", PerfEvent{eventstr}, NormStyle::PER_CL);
            cols.push_back(col);
        }
    }
    
#if USE_RDTSC
    fmt::print(out, "tsc_freq             : {} MHz ({})\n", RdtscClock::tsc_freq() / 1000000.0, get_tsc_cal_info(arg_force_tsc_cal));
#endif
    fmt::print(out, "Running as root      : {}\n", is_root     ? "YES" : "NO ");
    fmt::print(out, "CPU pinning enabled  : {}\n", !arg_no_pin ? "YES" : "NO ");
    fmt::print(out, "available CPUs ({:4}): {}\n", cpus.size(), join(cpus, ", "));
    fmt::print(out, "get_nprocs_conf()    : {}\n", get_nprocs_conf());
    fmt::print(out, "get_nprocs()         : {}\n", get_nprocs());
    fmt::print(out, "target size          : {}\n", arg_target_size.Get());
    fmt::print(out, "min buffer size      : {}\n", minsz);
    fmt::print(out, "max buffer size      : {}\n", maxsz);
    fmt::print(out, "step ratio           : {:.2f}\n", arg_step.Get());

    std::vector<test_func> algos;
    if (arg_algos) {
        auto algolist = split(arg_algos.Get(), ",");
        for (auto& algo : algolist) {
            auto it = std::find_if(ALL_FUNCS.begin(), ALL_FUNCS.end(), [=](test_func f){ return algo == f.id; });
            if ( it != ALL_FUNCS.end()) {
                algos.push_back(*it);
            } else {
                fmt::print(stderr, "Algorithm {} not found\n", algo);
                exit(EXIT_FAILURE);
            }
        }
    } else { 
        algos.insert(algos.begin(), std::begin(ALL_FUNCS), std::end(ALL_FUNCS));
    }

    // create a cache-line aligned buffer and initialize it
    auto maxelems = maxsz / sizeof(buf_elem);
    buf_elem* buf = static_cast<buf_elem*>(aligned_alloc(CACHE_LINE_BYTES, maxelems * sizeof(buf_elem)));
    std::fill(buf, buf + maxelems, -1);

    double step_frac = arg_step.Get();
    std::vector<test_spec> specs;
    size_t lastelem = (size_t)-1;
    for (size_t bytesz = minsz; bytesz <= maxsz; bytesz = bytesz * step_frac) {
        auto elemsz = bytesz / sizeof(buf_elem);
        if (elemsz == lastelem) {
            elemsz++;  // ensure that elemsize always advances
            bytesz = elemsz * sizeof(buf_elem);
        }
        lastelem = elemsz;

        auto iters = std::max((arg_target_size.Get() + bytesz - 1) / bytesz, arg_min_iters.Get());
        
        for (auto& algo : algos) {
            test_spec s = {algo, iters, buf, elemsz};
            specs.push_back(s);
        }
    }

    // point jevents to the right location for the event files
    setenv("JEVENTS_CACHEDIR", ".", 0);

    StampConfig config;
    // we give each rh_column a chance to update the StampConfig with what it needs
    for (auto& col : cols) {
        col->update_config(config);
    }
    config.prepare();

    std::vector<result_holder> results_list;
    fmt::print(out, "Running total {} benchmark specs\n", specs.size());
    for (auto& spec : specs) {
        results_list.push_back(run_test(spec, config));
    }

    report_results(cols, results_list);

    return EXIT_SUCCESS;
}




