QEMU Python Tooling
===================

This directory houses Python tooling used by the QEMU project to build,
configure, and test QEMU. It is organized by namespace (``qemu``), and
then by package (e.g. ``qemu/machine``, ``qemu/utils``, etc).

These tools and libraries are installed to the QEMU configure-time
Python virtual environment by default (see qemu.git/pythondeps.toml
"tooling" group), and are available for use by any Python script
executed by the build system. To have these libraries available for
manual invocations of scripts, use of the "run" script in your build
directory is recommended.

General structure
-----------------

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
a forwarder that points to the source tree. In so doing, the installed
package always reflects the latest version in your source tree. This is
the mode used to install these packages at configure time.

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

It is no longer recommended to try to use these packages without
installing them to a virtual environment, but depending on your use
case, it may still be possible to do.

The "qemu.qmp" library is now hosted outside of the qemu.git repository,
and the "qemu.machine" library that remains in-tree here has qemu.qmp as
a dependency. It is possible to install "qemu.qmp" independently and
then use the rest of these packages without installing them, but be
advised that if future dependencies are introduced, bypassing the
installation phase may introduce breakages to your script in the future.

That said, you can use these packages without installing them by either:

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

**It is strongly advised to just use the configure-time venv instead.**
After running configure, simply use the run script available in the QEMU
build directory:

``> $builddir/run qmp-shell``

The mappings between console script name and python module path can be
found in ``setup.cfg``, but the console scripts available are listed
here for reference:

* ``qemu-ga-client``
* ``qmp-shell``
* ``qmp-shell-wrap``
* ``qmp-tui`` (prototype urwid interface for async QMP)
* ``qom``
* ``qom-fuse`` (requires fusepy to be installed!)
* ``qom-get``
* ``qom-list``
* ``qom-set``
* ``qom-tree``


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
