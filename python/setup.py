#!/usr/bin/env python3
"""
QEMU tooling installer script
Copyright (c) 2020-2021 John Snow for Red Hat, Inc.
"""

import setuptools
import pkg_resources


def main():
    """
    QEMU tooling installer
    """

    # https://medium.com/@daveshawley/safely-using-setup-cfg-for-metadata-1babbe54c108
    pkg_resources.require('setuptools>=39.2')

    setuptools.setup()


if __name__ == '__main__':
    main()
