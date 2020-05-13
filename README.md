This benchmark illustrates _hardware store elimination_ on Intel Skylake-S processors, as described in [this blog post](https://travisdowns.github.io/blog/2020/05/13/intel-zero-opt.html).

It requires a Linux system to build, and has been tested on Ubuntu 19.04, CentOS 7, Debian 10.

## Building

    make
    
## Running

    ./bench [options]
   
The list of available options can be obtained by running `./bench --help`. Currently, they are:

~~~
  ./bench {OPTIONS}

    zero-fill-bench: Demonstrate concurrency perforamnce levels

  OPTIONS:

      --help                            Display this help menu
      --force-tsc-calibrate             Force manual TSC calibration loop, even
                                        if cpuid TSC Hz is available
      --no-pin                          Don't try to pin threads to CPU - gives
                                        worse results but works around affinity
                                        issues on TravisCI
      --verbose                         Output more info
      --list                            List the available tests and their
                                        descriptions
      --csv                             Output a csv table instead of the
                                        default
      --algos=[ALGO1,ALGO2,...]         Run only the algorithms in the comma
                                        separated list
      --perf-cols=[COL1,COL2,...]       Include the additional perf-event based
                                        columns
      --perf-extra=[EVENT1,EVENT2,...]  Include the additional arbitrary perf
                                        events
      --trial-size=[SIZE]               Target size in bytes for each trial,
                                        used to calculate internal iters
      --min-iters=[ITERS]               Minimum number of internal iteratoins
                                        for each trial (default 2)
      --warmup-ms=[MILLISECONDS]        Warmup milliseconds for each thread
                                        after pinning (default 100)
      --min-size=[KILOBYTES]            Minimum buffer size in bytes
      --max-size=[KILOBYTES]            Maximum buffer size in bytes
      --size=[KILOBYTES]                Buffer size (overrides min and max)
      --step=[RATIO]                    Possibly factional ratio between
                                        successive sizes
~~~

## Data Collection

You can examine the scripts in scripts/ to see how the data was collected and plotted. You'll need to have `perf_event_open`
access on your system (`perf_event_paranoid` set to `<= 2` and all that).
