.PHONY: help venv venv-check check clean distclean develop

help:
	@echo "python packaging help:"
	@echo ""
	@echo "make venv:       Create pipenv's virtual environment."
	@echo "    NOTE: Requires Python 3.6 and pipenv."
	@echo "          Will download packages from PyPI."
	@echo "    Hint: (On Fedora): 'sudo dnf install python36 pipenv'"
	@echo ""
	@echo "make venv-check: run linters using pipenv's virtual environment."
	@echo "    Hint: If you don't know which test to run, run this one!"
	@echo ""
	@echo "make develop:    Install deps for 'make check', and"
	@echo "                 the qemu libs in editable/development mode."
	@echo ""
	@echo "make check:      run linters using the current environment."
	@echo ""
	@echo "make check-tox:  run linters using multiple python versions."
	@echo ""
	@echo "make clean:      remove package build output."
	@echo ""
	@echo "make distclean:  remove venv files, qemu package forwarder,"
	@echo "                 built distribution files, and everything"
	@echo "                 from 'make clean'."

venv: .venv
.venv: Pipfile.lock
	@PIPENV_VENV_IN_PROJECT=1 pipenv sync --dev --keep-outdated
	@touch .venv

venv-check: venv
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
