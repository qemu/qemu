#!/bin/sh -e
# See commit message for environment variable explainer.
SETUPTOOLS_USE_DISTUTILS=stdlib python3 -m pylint qemu/
SETUPTOOLS_USE_DISTUTILS=stdlib python3 -m pylint scripts/
