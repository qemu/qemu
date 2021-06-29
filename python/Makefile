.PHONY: help pipenv check-pipenv check clean distclean develop

help:
	@echo "python packaging help:"
	@echo ""
	@echo "make check-pipenv:"
	@echo "    Run tests in pipenv's virtual environment."
	@echo "    These tests use the oldest dependencies."
	@echo "    Requires: Python 3.6 and pipenv."
	@echo "    Hint (Fedora): 'sudo dnf install python3.6 pipenv'"
	@echo ""
	@echo "make check-tox:"
	@echo "    Run tests against multiple python versions."
	@echo "    These tests use the newest dependencies."
	@echo "    Requires: Python 3.6 - 3.10, and tox."
	@echo "    Hint (Fedora): 'sudo dnf install python3-tox python3.10'"
	@echo ""
	@echo "make develop:    Install deps for 'make check', and"
	@echo "                 the qemu libs in editable/development mode."
	@echo ""
	@echo "make check:      run linters using the current environment."
	@echo ""
	@echo "make pipenv"
	@echo "    Creates pipenv's virtual environment (.venv)"
	@echo ""
	@echo "make clean:      remove package build output."
	@echo ""
	@echo "make distclean:  remove venv files, qemu package forwarder,"
	@echo "                 built distribution files, and everything"
	@echo "                 from 'make clean'."

pipenv: .venv
.venv: Pipfile.lock
	@PIPENV_VENV_IN_PROJECT=1 pipenv sync --dev --keep-outdated
	@touch .venv

check-pipenv: pipenv
	@pipenv run make check

develop:
	pip3 install -e .[devel]

check:
	@avocado --config avocado.cfg run tests/

check-tox:
	@tox

clean:
	python3 setup.py clean --all

distclean: clean
	rm -rf qemu.egg-info/ .venv/ .tox/ dist/
