#!/bin/sh -e
python3 -m mypy -p qemu
python3 -m mypy scripts/
