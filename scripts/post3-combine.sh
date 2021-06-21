#!/bin/bash

# combine the results from e2 and ea microcode runs into a single
# file with renamed 'algo' column

set -euo pipefail

echo "RDIR=${RDIR:=./results/post3}"
echo "OUTDIR=${OUTDIR:=./results/post3/skl-combined}"

mkdir -p $OUTDIR

OUT=$RDIR/skl-combined/l2-focus.csv
head -1 $RDIR/skl-e2/l2-focus.csv > $OUT

function replace {
    local mc=$1
    local day=$2
    sed -r "s/,(fill[01]|alt01),/,\1 $day,/g" "$RDIR/skl-$mc/l2-focus.csv" | tail +2 >> $OUT
}

replace e2 Tuesday
replace ea Wednesday
