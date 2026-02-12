#!/usr/bin/env python3
"""
QEMU tooling installer script
Copyright (c) 2020-2021 John Snow for Red Hat, Inc.
"""

import setuptools
from setuptools.command import bdist_egg
import sys


class bdist_egg_guard(bdist_egg.bdist_egg):
    """
    Protect against bdist_egg from being executed

    This prevents calling 'setup.py install' directly, as the 'install'
    CLI option will invoke the deprecated bdist_egg hook. "pip install"
    calls the more modern bdist_wheel hook, which is what we want.
    """
    def run(self):
        sys.exit(
            'Installation directly via setup.py is not supported.\n'
            'Please use `pip install .` instead.'
        )


def main():
    """
    QEMU tooling installer
    """

    setuptools.setup(cmdclass={'bdist_egg': bdist_egg_guard})


if __name__ == '__main__':
    main()
