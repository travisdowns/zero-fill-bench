#!/bin/bash
set -euo pipefail

SUFFIX=${1-""}

echo "RDIR=${RDIR:=./results} SUFFIX=${SUFFIX}"

mkdir -p results

echo "Collecting data in $RDIR"
./bench --algos=fill0,fill1,fill01 --csv > "$RDIR/remote$SUFFIX.csv"