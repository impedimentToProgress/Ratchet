#!/bin/bash

roots=("crc/" "rsa" "FFT" "dijkstra" "basicmath" "stringsearch" "sha"
"picojpeg")

for i in "${roots[@]}"
do
  rm "$i/results/*.txt"
done
