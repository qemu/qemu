# SPDX-License-Identifier: GPL-2.0-or-later

import os
from subprocess import check_call
import sys


class TestLinters:

    def test_flake8_pkg(self):
        check_call([sys.executable, "-m", "flake8", "qemu/"])

    def test_flake8_scripts(self):
        check_call([sys.executable, "-m", "flake8", "scripts/"])

    def test_flake8_qapi(self):
        check_call(
            [
                sys.executable,
                "-m",
                "flake8",
                "../scripts/qapi/",
                "../docs/sphinx/qapidoc.py",
                "../docs/sphinx/qapi_domain.py",
            ]
        )

    def test_isort_pkg(self):
        check_call([sys.executable, "-m", "isort", "-c", "qemu/"])

    def test_isort_scripts(self):
        check_call([sys.executable, "-m", "isort", "-c", "scripts/"])

    def test_isort_qapi(self):
        check_call(
            [
                sys.executable,
                "-m",
                "isort",
                "--sp",
                ".",
                "-c",
                "../scripts/qapi/",
            ]
        )

    def test_isort_qapi_sphinx(self):
        # Force isort to recognize 'compat' as a local module and not
        # third-party
        check_call(
            [
                sys.executable,
                "-m",
                "isort",
                "--sp",
                ".",
                "-c",
                "-p",
                "compat",
                "../docs/sphinx/qapi_domain.py",
                "../docs/sphinx/qapidoc.py",
            ]
        )

    def test_mypy_pkg(self):
        check_call([sys.executable, "-m", "mypy", "-p", "qemu"])

    def test_mypy_scripts(self):
        check_call([sys.executable, "-m", "mypy", "scripts/"])

    def test_mypy_qapi(self):
        check_call([sys.executable, "-m", "mypy", "../scripts/qapi"])

    def test_mypy_iotests(self):
        check_call(
            [sys.executable, "-m", "linters", "--mypy"],
            cwd="../tests/qemu-iotests/",
        )

    # Setuptools v60 introduced the SETUPTOOLS_USE_DISTUTILS=stdlib
    # workaround; stdlib distutils was fully removed in Python
    # 3.12+. Once we are on >=3.12+ exclusively, this workaround can be
    # dropped safely. Until then, it is needed for some versions on
    # Fedora/Debian distributions which relied upon distro-patched
    # setuptools present in CPython, but not within setuptools itself.

    def test_pylint_pkg(self):
        os.environ["SETUPTOOLS_USE_DISTUTILS"] = "stdlib"
        check_call([sys.executable, "-m", "pylint", "qemu/"])

    def test_pylint_scripts(self):
        os.environ["SETUPTOOLS_USE_DISTUTILS"] = "stdlib"
        check_call([sys.executable, "-m", "pylint", "scripts/"])

    def test_pylint_qapi(self):
        os.environ["SETUPTOOLS_USE_DISTUTILS"] = "stdlib"
        check_call(
            [
                sys.executable,
                "-m",
                "pylint",
                "--rcfile=../scripts/qapi/pylintrc",
                "../scripts/qapi/",
                "../docs/sphinx/qapidoc.py",
                "../docs/sphinx/qapi_domain.py",
            ]
        )

    def test_pylint_iotests(self):
        os.environ["SETUPTOOLS_USE_DISTUTILS"] = "stdlib"
        check_call(
            [sys.executable, "-m", "linters", "--pylint"],
            cwd="../tests/qemu-iotests/",
        )
