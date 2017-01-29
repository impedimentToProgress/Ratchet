#!/bin/bash

ROOTS=( "crc/" "rsa/" "FFT/" "dijkstra/" "picojpeg/" "stringsearch/" "sha/" "basicmath/" )
#ROOTS=( "crc/" "rsa/" )
OPT='OPTLVL=-O3'

function memstats {
  ret=`../sim_main $1/main.bin 2>&1 | grep 'Loads:\|Stores:\|Checkpoints:' | awk '{print $2}'`
  echo -n $ret
}

function remake {
  MAKECMD="(cd $1; make clean; $2 make; cd -) &> /dev/null" 
  local ret=$(eval $MAKECMD)
  echo -n $ret
}

TOTAL=0

function bench {
  remake $root "$OPT NOIDEMCOMP=0"
  RESULT1=$(eval memstats $root)
  remake $root "$OPT NOIDEMCOMP=1"
  RESULT2=$(eval memstats $root)
  echo "$root,$RESULT1,$RESULT2" >> memory.results
}

function benchall {
  for root in "${ROOTS[@]}"
  do
    bench $root &
  done
  
  wait
}

rm memory.results
benchall
cat memory.results
