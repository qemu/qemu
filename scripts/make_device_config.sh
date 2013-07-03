#! /bin/sh
# Construct a target device config file from a default, pulling in any
# files from include directives.

dest=$1.tmp
dep=`dirname $1`-`basename $1`.d
src=$2
src_dir=`dirname $src`
all_includes=

process_includes () {
  cat $1 | grep '^include' | \
  while read include file ; do
    all_includes="$all_includes $src_dir/$file"
    process_includes $src_dir/$file
  done
}

f=$src
while [ -n "$f" ] ; do
  f=`cat $f | tr -d '\r' | awk '/^include / {printf "'$src_dir'/%s ", $2}'`
  [ $? = 0 ] || exit 1
  all_includes="$all_includes $f"
done
process_includes $src > $dest

cat $src $all_includes | grep -v '^include' > $dest
echo "$1: $all_includes" > $dep
