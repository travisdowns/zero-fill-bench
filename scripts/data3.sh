#!/bin/bash

# data for post3

set -euo pipefail

echo "RDIR=${RDIR:=./results}"

mkdir -p "$RDIR"

d='---------------------------'
# up to CPU-count threads
echo -e "$d\nCollecting data in $RDIR\n$d"
./bench --algos=fill0,fill1,alt01 --min-size=40000  --max-size=100000000 --perf-cols=l2-out-silent,l2-out-non-silent --step=1.05 --csv > "$RDIR/l2-focus.csv"
echo -e "$d\nData collection finished!\n$d"