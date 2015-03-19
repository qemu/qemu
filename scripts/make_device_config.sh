#! /bin/sh
# Writes a target device config file to stdout, from a default and from
# include directives therein.  Also emits Makefile dependencies.
#
# Usage: make_device_config.sh SRC DEPFILE-NAME DEPFILE-TARGET > DEST

src=$1
dep=$2
target=$3
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
process_includes $src

cat $src $all_includes | grep -v '^include'
echo "$target: $all_includes" > $dep
