#!/bin/sh
config="$1"
make -C seabios clean distclean
cp "$config" seabios/.config
make -C seabios oldnoconfig
