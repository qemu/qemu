#! /bin/sh
# Script to check for duplicate function prologues in op.o
# Typically this indicates missing FORCE_RET();
# This script does not detect other errors that may be present.

# Usage: check_ops.sh [-m machine] [op.o]
#   machine and op.o are guessed if not specified.

if [ "x$1" = "x-m" ]; then
  machine=$2
  shift 2
else
  machine=`uname -m`
fi
if [ -z "$1" ]; then
  for f in `find . -name op.o`; do
    /bin/sh "$0" -m $machine $f
  done
  exit 0
fi

case $machine in
  i?86)
    ret='\tret'
    ;;
  x86_64)
    ret='\tretq'
    ;;
  arm)
    ret='\tldm.*pc'
    ;;
  ppc* | powerpc*)
    ret='\tblr'
    ;;
  mips*)
    ret='\tjr.*ra'
    ;;
  s390*)
    ret='\tbr.*'
    ;;
  *)
    echo "Unknown machine `uname -m`"
    ;;
esac
echo $1
# op_exit_tb causes false positives on some hosts.
${CROSS}objdump -dr $1  | \
  sed -e '/>:$\|'"$ret"'/!d' -e 's/.*<\(.*\)>:/~\1:/' -e 's/.*'"$ret"'.*/!/' | \
  sed -e ':1;N;s/\n//;t1' | sed -e 's/~/\n/g' | grep -v '^op_exit_tb' | \
  grep '^op_.*!!'
