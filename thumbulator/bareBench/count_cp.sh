#!/bin/bash

function get_funcname {
  grep -i "$addr" rsa/main.lst | awk '{print $2}'
}

function get_addr {
  echo `get_funcname "$1"`
}

function get_count {
  echo "$1"
  cmd="../sim_main $1/main.bin 2>&1 | grep 'CP: [0-9]*, Caller:' | awk '{print \$1}' | sort | uniq -c"
  COUNT=`eval $cmd`
  echo "$COUNT"
}

get_count $1

#ADDRESSES=`echo "$COUNT" | awk '{print $2}' | sed 's/://'`

#ROOTS=("crc/" "rsa/" "FFT/" "dijkstra/" "picojpeg/" "stringsearch/" "sha/" "basicmath/" )
#
#for root in $ROOTS;
#do
#  get_count $root
#done

#for addr in $ADDRESSES;
#do
#  FNAME=`get_funcname $addr`
#  COUNT=`echo $COUNT | sed 's/'$addr'\n/'$FNAME'\n/'`
#  echo $COUNT
#done
#COUNT=`echo $COUNT | sed 's/://'`
#echo "$COUNT"

#for line in `"$COUNT"`;
#do
#  #addr=`echo $line | awk '{print $2}' | sed 's/://'`
#  echo $line
#  #echo "`echo $line | awk '{print $1}'`" "`get_funcname $addr`"
#done

