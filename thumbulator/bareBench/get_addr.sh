#!/bin/bash

grep '<_checkpoint_\([0-8]\|ret\)>:' "$1"/main.lst | awk '{print }' | sed 's/$//' | sed 's/^/case 0x/'
