#!/usr/bin/env bash
#
# This script can be used to plot frame time layer output with gnuplot. A PNG is
# generated containing a CDF of frame times in milliseconds. Any number of input
# files can be specified and by default the output is written to frametime.png.
#
# Usage:
#   plot_fram_times.sh frame_time_a.txt frame_time_b.txt

set -e

XTICS=${XTICS:-5}
YTICS=${YTICS:-0.1}
XRANGE=${XRANGE:-50}
OUTFILE=${OUTFILE:-frametime.png}

plot="plot "
comma=""
for cur in $@
do
  lines=$(wc -l "${cur?}" | awk '{print $1}')
  plot="${plot?}${comma?}'${cur?}' u (\$1/1000000):(1/${lines?}.) smooth cumulative w l t '${cur?}'"
  comma=", "
done

echo -e 'set term png size 1920,1080;' \
    "set out '${OUTFILE?}';" \
    'set key autotitle columnhead;' \
    "set ytics ${YTICS?};" \
    "set xtics ${XTICS?};" \
    'set xlabel "Frame Time (ms)";' \
    'set ylabel "Percentile";' \
    "set xrange [0:${XRANGE?}];" \
    "${plot?}" \
    | gnuplot

