#!/bin/bash
####################################################
## Collect data for the 256-bit vs 512-bit effect ##
####################################################

set -euo pipefail

echo "RDIR=${RDIR:=./results}"

mkdir -p "$RDIR"

d='---------------------------'
# up to CPU-count threads
echo -e "$d\nCollecting data in $RDIR\n$d"
./bench --algos=fill256_0,fill512_0,fill256_1,fill512_1 --perf-cols=GHz --csv --warmup-ms=0 > "$RDIR/256-512.csv"
./bench --algos=fill256_0,fill512_0,fill256_1,fill512_1 --min-size=60000  --max-size=10000000 --step=1.1 --perf-cols=GHz --csv --warmup-ms=0 > "$RDIR/256-512-l2-l3.csv"
echo -e "$d\nData collection finished!\n$d"