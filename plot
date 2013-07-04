#!/bin/bash
if [ -z $1 ]; then
    number_of_samples=50000
else
    number_of_samples=$1
fi
gnuplot -e "plot '/tmp/microphone' binary format=\"%float%float\" array=$number_of_samples using 1 with lines"
