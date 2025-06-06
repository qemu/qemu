#!/bin/sh -e
# SPDX-License-Identifier: GPL-2.0-or-later

SETUPTOOLS_USE_DISTUTILS=stdlib python3 -m pylint \
                                --rcfile=../scripts/qapi/pylintrc \
                                ../scripts/qapi/ \
                                ../docs/sphinx/qapidoc.py \
                                ../docs/sphinx/qapi_domain.py
