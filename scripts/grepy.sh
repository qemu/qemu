#!/bin/sh

grep -h '=y$' "$@" | sort -u
