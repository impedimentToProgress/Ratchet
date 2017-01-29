#!/bin/bash

#postfixs=('empty' '0' '1' '2' '3' '4' '5' '6' '7' '8' 'ret')
postfixs=('empty' '0' '1' '2' '3' '4' '5' '6' '7' '8')

rm results.txt

for i in "${postfixs[@]}";
do
  if [ "${i}" = "empty" ];
  then
    echo "void main() { __asm__ __volatile__ (\"nop\\n\\t\"); }" > main.c
  else
    echo "void main() { _checkpoint_${i}(); }" > main.c
  fi 
  make &> /dev/null
  sleep 1
  TIME=`../../sim_main main.bin 2>>/dev/null | grep "Program exit after [0-9]*" | awk '{print $4}'`
  make clean &> /dev/null

  if [ "${i}" = "empty" ];
  then
    EMPTY=$TIME
  else
    DIFF=$(($TIME-$EMPTY))
    echo "${i}: $DIFF" >> results.txt
  fi
  sleep 1
done
