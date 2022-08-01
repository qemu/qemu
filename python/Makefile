QEMU_VENV_DIR=.dev-venv
QEMU_TOX_EXTRA_ARGS ?=

.PHONY: help
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
	@echo "    The variable QEMU_TOX_EXTRA_ARGS can be use to pass extra"
	@echo "    arguments to tox".
	@echo ""
	@echo "make check-dev:"
	@echo "    Run tests in a venv against your default python3 version."
	@echo "    These tests use the newest dependencies."
	@echo "    Requires: Python 3.x"
	@echo ""
	@echo "make check:"
	@echo "    Run tests in your *current environment*."
	@echo "    Performs no environment setup of any kind."
	@echo ""
	@echo "make develop:"
	@echo "    Install deps needed for 'make check',"
	@echo "    and install the qemu package in editable mode."
	@echo "    (Can be used in or outside of a venv.)"
	@echo ""
	@echo "make pipenv"
	@echo "    Creates pipenv's virtual environment (.venv)"
	@echo ""
	@echo "make dev-venv"
	@echo "    Creates a simple venv for check-dev. ($(QEMU_VENV_DIR))"
	@echo ""
	@echo "make clean:"
	@echo "    Remove package build output."
	@echo ""
	@echo "make distclean:"
	@echo "    remove pipenv/venv files, qemu package forwarder,"
	@echo "    built distribution files, and everything from 'make clean'."
	@echo ""
	@echo -e "Have a nice day ^_^\n"

.PHONY: pipenv
pipenv: .venv
.venv: Pipfile.lock
	@PIPENV_VENV_IN_PROJECT=1 pipenv sync --dev --keep-outdated
	rm -f pyproject.toml
	@touch .venv

.PHONY: check-pipenv
check-pipenv: pipenv
	@pipenv run make check

.PHONY: dev-venv
dev-venv: $(QEMU_VENV_DIR) $(QEMU_VENV_DIR)/bin/activate
$(QEMU_VENV_DIR) $(QEMU_VENV_DIR)/bin/activate: setup.cfg
	@echo "VENV $(QEMU_VENV_DIR)"
	@python3 -m venv $(QEMU_VENV_DIR)
	@(							\
		echo "ACTIVATE $(QEMU_VENV_DIR)";		\
		. $(QEMU_VENV_DIR)/bin/activate;		\
		echo "INSTALL qemu[devel] $(QEMU_VENV_DIR)";	\
		make develop 1>/dev/null;			\
	)
	@touch $(QEMU_VENV_DIR)

.PHONY: check-dev
check-dev: dev-venv
	@(							\
		echo "ACTIVATE $(QEMU_VENV_DIR)";		\
		. $(QEMU_VENV_DIR)/bin/activate;		\
		make check;					\
	)

.PHONY: develop
develop:
	pip3 install --disable-pip-version-check -e .[devel]

.PHONY: check
check:
	@avocado --config avocado.cfg run tests/

.PHONY: check-tox
check-tox:
	@tox $(QEMU_TOX_EXTRA_ARGS)

.PHONY: check-coverage
check-coverage:
	@coverage run -m avocado --config avocado.cfg run tests/*.py
	@coverage combine
	@coverage html
	@coverage report

.PHONY: clean
clean:
	python3 setup.py clean --all
	rm -f pyproject.toml

.PHONY: distclean
distclean: clean
	rm -rf qemu.egg-info/ .venv/ .tox/ $(QEMU_VENV_DIR) dist/
	rm -f .coverage .coverage.*
	rm -rf htmlcov/
