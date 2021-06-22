This benchmark illustrates _hardware store elimination_ on Intel Skylake-S processors, as described in [this blog post](https://travisdowns.github.io/blog/2020/05/13/intel-zero-opt.html).

It requires a Linux system to build, and has been tested on Ubuntu 19.04, CentOS 7, Debian 10.

## Building

    make
    
## Running

    ./bench [options]
   
The list of available options can be obtained by running `./bench --help`. Currently, they are:

~~~
  ./bench {OPTIONS}

    bench: Demonstrate zero-fill optimizations

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
access on your system (`perf_event_paranoid` set to `<= 2` and all that). Details are provided below to reproduce results
from specific posts.

### Third Post: RIP Zero Store Optimization

Overview of reproducing the data in the [third post](https://travisdowns.github.io/blog/2021/06/17/rip-zero-opt.html).

`make` the benchmark binary as described [above](#building).

You'll need two separate runs to collect the data for the old and new microcode. The easiest way to install the microcode versions is to boot with the
`dis_ucode_ldr` kernel parameter, which disables OS updating of the microcode. This will give you whatever microcode is applied by your BIOS (or the microcode your CPU came with if your BIOS doesn't apply any update).

Then you can install the microcode you want using the `/sys/devices/system/cpu/microcode/reload` interface. This is described in more detail on Intel's [microcode repository](https://github.com/intel/Intel-Linux-Processor-Microcode-Data-Files), which is also where you can fetch every
released microcode version. You can test any number of microcode versions in this way, as long as you go from _oldest_ to _newest_ microcode (you can load an old microcode on top of a new one). It is possible that some microcode features don't work when loaded when the systme is running, but the behavior discussed here doesn't seem to have a problem with dynamically reloaded microcode. 

Once you have the targeted microcode installed (check via `grep microcode /proc/cpuinfo`), collect the results as shown below for Skylake microcde
version `0xea`:

~~~
RDIR=./results/post3/skl-ea scripts/data3.sh
~~~

Similarly, to collect the results for microcode `0xe2`:

~~~
RDIR=./results/post3/skl-e2 scripts/data3.sh
~~~

Combine and rename the results:

~~~
scripts/post3-combine.sh
~~~

This script assumes you want to combine the same microcode versions as I have used, but it's easy to modify for your own versions.

Finally, generate the plots:

~~~
/scripts/make-plots.py --post3
~~~

This looks for the combined results in `./results/post3/skl-combined` and `./results/post3/icl-combined`, but it is easy to modify it if you chose a different location.



