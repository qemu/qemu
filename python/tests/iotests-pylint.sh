#!/bin/sh -e

cd ../tests/qemu-iotests/
python3 -m linters --pylint
