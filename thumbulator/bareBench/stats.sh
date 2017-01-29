#!/bin/bash

ROOTS=( "crc/" "rsa/" "FFT/" "dijkstra/" "picojpeg/" "stringsearch/" "sha/" "basicmath/" )
#ROOTS=( "crc/" "rsa/" )
OPT='OPTLVL=-O3'

function runtime {
  ret=`../sim_main $1/main.bin 2>&1 | grep 'Program exit after [0-9]*'  | awk '{print $4}'`
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
  RESULT1=$(eval runtime $root)
  remake $root "$OPT NOIDEMCOMP=1"
  RESULT2=$(eval runtime $root)
  TIMEOVER=$(bc -l <<< $RESULT1/$RESULT2)
  echo "$root,$RESULT1,$RESULT2" >> benchmark.results
  echo $TIMEOVER
}

function benchall {
  for root in "${ROOTS[@]}"
  do
    bench $root &
  done
  
  wait
}

rm benchmark.results
SUM=$(benchall | paste -sd+  - | bc) 
echo $(bc -l <<< $SUM/${#ROOTS[@]})
