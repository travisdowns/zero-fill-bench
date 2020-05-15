#!/bin/bash
set -euo pipefail

echo "RDIR=${RDIR:=./results}"

mkdir -p "$RDIR"

d='---------------------------'
# up to CPU-count threads
echo -e "$d\nCollecting data in $RDIR\n$d"
./bench --algos=fill0,fill1 --perf-cols=GHz --csv > "$RDIR/overall-warm.csv"
./bench --algos=fill0,fill1 --perf-cols=GHz --csv --warmup-ms=0 > "$RDIR/overall.csv"
./bench --algos=fill0,fill1,alt01 --min-size=60000  --max-size=100000000 --warmup-ms=0 --perf-cols=l2-out-silent,l2-out-non-silent,GHz --step=1.1 --csv > "$RDIR/l2-focus.csv"
./bench --algos=fill0,fill1,alt01 --min-size=600000 --max-size=400000000 --warmup-ms=0 --perf-cols=uncR,uncW --step=1.1 --csv > "$RDIR/l3-focus.csv"
echo -e "$d\nData collection finished!\n$d"