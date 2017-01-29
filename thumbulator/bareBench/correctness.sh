#!/bin/bash

ROOTS=( "crc/" "rsa/" "FFT/" "dijkstra/" "picojpeg/" "stringsearch/" "sha/" "basicmath/" )
#ROOTS=( "dijkstra/" )
#ROOTS=( "stringsearch/" )

function correctness {
  cmd="(../sim_main $1/main.bin 2>&1 | grep
  [0-9]*,[0-9]*,[0-9]*,[0-9]*,[0-9]*,[0-9]* | grep -v
  [0-9]*,[0-9]*,0,[0-9]*,[0-9]*,[0-9]*)"
  RESULT=$(eval $cmd)
  if [ -n "$RESULT" ]; then
    echo "$1 has idempotency violations!"
  else
    echo "$1 is okay!"
  fi
}

echo "PID: $$"

for root in "${ROOTS[@]}"
do
  #cmd="(../sim_main $root/main.bin 2>&1 | python check_idem.py - 0x40000000 > $root/results/correctness.txt) &" 
  correctness $root &
done

wait

echo "All done!"
