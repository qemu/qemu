#!/bin/sh

sha512sum < $1 | cut -d ' ' -f 1
