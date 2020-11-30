#!/bin/bash

export PYTHONMALLOC=malloc
export PYTHONPATH=../../../build/debug/src/py

valgrind --suppressions=valgrind-pycamera.supp --leak-check=full --show-leak-kinds=definite --gen-suppressions=yes python3 test.py $*