#!/bin/sh -e
# SPDX-License-Identifier: GPL-2.0-or-later

python3 -m flake8 ../scripts/qapi/ \
        ../docs/sphinx/qapidoc.py \
        ../docs/sphinx/qapi_domain.py
