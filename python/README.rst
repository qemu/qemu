QEMU Python Tooling
===================

This directory houses Python tooling used by the QEMU project to build,
configure, and test QEMU. It is organized by namespace (``qemu``), and
then by package (e.g. ``qemu/machine``, ``qemu/qmp``, etc).

``setup.py`` is used by ``pip`` to install this tooling to the current
environment. ``setup.cfg`` provides the packaging configuration used by
``setup.py``. You will generally invoke it by doing one of the following:

1. ``pip3 install .`` will install these packages to your current
   environment. If you are inside a virtual environment, they will
   install there. If you are not, it will attempt to install to the
   global environment, which is **not recommended**.

2. ``pip3 install --user .`` will install these packages to your user's
   local python packages. If you are inside of a virtual environment,
   this will fail; you want the first invocation above.

If you append the ``--editable`` or ``-e`` argument to either invocation
above, pip will install in "editable" mode. This installs the package as
a forwarder ("qemu.egg-link") that points to the source tree. In so
doing, the installed package always reflects the latest version in your
source tree.

Installing ".[devel]" instead of "." will additionally pull in required
packages for testing this package. They are not runtime requirements,
and are not needed to simply use these libraries.

Running ``make develop`` will pull in all testing dependencies and
install QEMU in editable mode to the current environment.
(It is a shortcut for ``pip3 install -e .[devel]``.)

See `Installing packages using pip and virtual environments
<https://packaging.python.org/guides/installing-using-pip-and-virtual-environments/>`_
for more information.


Using these packages without installing them
--------------------------------------------

These packages may be used without installing them first, by using one
of two tricks:

1. Set your PYTHONPATH environment variable to include this source
   directory, e.g. ``~/src/qemu/python``. See
   https://docs.python.org/3/using/cmdline.html#envvar-PYTHONPATH

2. Inside a Python script, use ``sys.path`` to forcibly include a search
   path prior to importing the ``qemu`` namespace. See
   https://docs.python.org/3/library/sys.html#sys.path

A strong downside to both approaches is that they generally interfere
with static analysis tools being able to locate and analyze the code
being imported.

Package installation also normally provides executable console scripts,
so that tools like ``qmp-shell`` are always available via $PATH. To
invoke them without installation, you can invoke e.g.:

``> PYTHONPATH=~/src/qemu/python python3 -m qemu.qmp.qmp_shell``

The mappings between console script name and python module path can be
found in ``setup.cfg``.


Files in this directory
-----------------------

- ``qemu/`` Python 'qemu' namespace package source directory.
- ``tests/`` Python package tests directory.
- ``avocado.cfg`` Configuration for the Avocado test-runner.
  Used by ``make check`` et al.
- ``Makefile`` provides some common testing/installation invocations.
  Try ``make help`` to see available targets.
- ``MANIFEST.in`` is read by python setuptools, it specifies additional files
  that should be included by a source distribution.
- ``PACKAGE.rst`` is used as the README file that is visible on PyPI.org.
- ``README.rst`` you are here!
- ``VERSION`` contains the PEP-440 compliant version used to describe
  this package; it is referenced by ``setup.cfg``.
- ``setup.cfg`` houses setuptools package configuration.
- ``setup.py`` is the setuptools installer used by pip; See above.
