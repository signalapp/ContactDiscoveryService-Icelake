#!/bin/bash
#
# Copyright 2022 Signal Messenger, LLC
# SPDX-License-Identifier: AGPL-3.0-only
#
# This script sets up and runs AFL fuzzing (https://lcamtuf.coredump.cx/afl/)
# on CDSI.  Arguments to this script are passed into afl-fuzz.

set -x

if ! which afl-fuzz; then
  sudo apt-get install -y afl++
fi

cd "$(dirname "$0")"
echo "Making environment"
BASEDIR=`pwd`/aflfuzz
OUTPUT_DIR=$BASEDIR/findings
INPUT_DIR="-"
mkdir -p $OUTPUT_DIR
if [ ! -d $BASEDIR/testcases ]; then
  echo "Generating new test cases"
  INPUT_DIR=$BASEDIR/testcases
  mkdir -p $INPUT_DIR
  for i in {0..7}; do
    echo -e -n "\\00$i" > $INPUT_DIR/$i
  done
fi

if ! egrep -q "^core$" /proc/sys/kernel/core_pattern; then
  echo core | sudo tee /proc/sys/kernel/core_pattern
fi
for f in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  if ! egrep -q "^performance$" $f; then
    echo performance | sudo tee $f
  fi
done

# Have AFL tell us which (new) test cases are dumb and we can throw away
echo "Running fuzzer"

PIDS=""
function kill_fuzzers {
  kill $PIDS || true
}
trap kill_fuzzers EXIT

# Run multiple parallel fuzzers.  Note that we 'nice' all of these, which
# makes the system we're running on still usable for other tasks while the
# fuzzers are running.  Removing 'nice' here may increase the performance
# slightly of the fuzzers themselves, but it will decrease usability of other
# programs running in parallel on the same CPUs.
CPUS="$(cat /proc/cpuinfo | grep processor | wc -l)"
# Run the master in the shell we're executing in.  This gives us the ability to
# see any errors it might pop out.
AFL_BIN=./fuzz.bin
nice afl-fuzz -i $INPUT_DIR -o $OUTPUT_DIR -M fuzz1 $@ -- $AFL_BIN & 
PIDS="$PIDS $!"

XTERM=""
if which xterm; then
  XTERM="xterm -e"
fi
for (( i=2 ; i<=$CPUS ; i++ )); do
  FLAG="-S"
  # Run the secondary processors (all the other cores) in xterms, so we can see
  # their progress independently.  If they crash, though, we'll lose their
  # output logs.
  $XTERM bash -c "nice afl-fuzz -i $INPUT_DIR -o $OUTPUT_DIR $FLAG fuzz$i $@ -- $AFL_BIN" & 
  PIDS="$PIDS $!"
done
wait
