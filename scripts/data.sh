#!/bin/bash
set -euo pipefail

echo "RDIR=${RDIR:=./results}"

mkdir -p results

d='---------------------------'
# up to CPU-count threads
echo -e "$d\nCollecting data in $RDIR\n$d"
./bench --algos=fill0,fill1 --csv > "$RDIR/overall.csv"
./bench --algos=fill0,fill1,alt01 --min-size=40000  --max-size=100000000 --perf-cols=l2-out-silent,l2-out-non-silent --step=1.1 --csv > "$RDIR/l2-focus.csv"
./bench --algos=fill0,fill1,alt01 --min-size=300000 --max-size=400000000 --perf-cols=uncR,uncW --step=1.1 --csv > "$RDIR/l3-focus.csv"
echo -e "$d\nData collection finished!\n$d"