#!/bin/sh -e

cd ../tests/qemu-iotests/
# See commit message for environment variable explainer.
SETUPTOOLS_USE_DISTUTILS=stdlib python3 -m linters --pylint
