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

    def test_isort_minikconf(self):
        check_call([sys.executable, "-m", "isort", "-c", "../scripts/minikconf.py"])

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

    def test_mypy_minikconf(self):
        check_call([sys.executable, "-m", "mypy", "../scripts/minikconf.py"])

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

    def test_pylint_pkg(self):
        check_call([sys.executable, "-m", "pylint", "qemu/"])

    def test_pylint_scripts(self):
        check_call([sys.executable, "-m", "pylint", "scripts/"])

    def test_pylint_qapi(self):
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
        check_call(
            [sys.executable, "-m", "linters", "--pylint"],
            cwd="../tests/qemu-iotests/",
        )
