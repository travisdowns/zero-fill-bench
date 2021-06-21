#!/bin/bash

# combine the results from e2 and ea microcode runs into a single
# file with renamed 'algo' column

set -euo pipefail

echo "RDIR=${RDIR:=./results/post3}"

function prep {
    local uarch=$1
    local mc=$2
    local out=$RDIR/$uarch-combined/l2-focus.csv
    mkdir -p "$(dirname "$out")"
    head -1 "$RDIR/$uarch-$mc/l2-focus.csv" > "$out"
} 

function replace {
    local uarch=$1
    local mc=$2
    local day=$3
    local out=$RDIR/$uarch-combined/l2-focus.csv
    sed -r "s/,(fill[01]|alt01),/,\1 $day,/g" "$RDIR/$uarch-$mc/l2-focus.csv" | tail +2 >> "$out"
}

prep    skl e2
replace skl e2 Tuesday
replace skl ea Wednesday

prep    icl a0
replace icl a0 Tuesday
replace icl a6 Wednesday

