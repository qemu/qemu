.. _checkfunctional-ref:

Functional testing with Python
==============================

The ``tests/functional`` directory hosts functional tests written in
Python. They are usually higher level tests, and may interact with
external resources and with various guest operating systems.

The tests should be written in the style of the Python `unittest`_ framework,
using stdio for the TAP protocol. The folder ``tests/functional/qemu_test``
provides classes (e.g. the ``QemuBaseTest``, ``QemuUserTest`` and the
``QemuSystemTest`` classes) and utility functions that help to get your test
into the right shape, e.g. by replacing the 'stdout' python object to redirect
the normal output of your test to stderr instead.

Note that if you don't use one of the QemuBaseTest based classes for your
test, or if you spawn subprocesses from your test, you have to make sure
that there is no TAP-incompatible output written to stdio, e.g. either by
prefixing every line with a "# " to mark the output as a TAP comment, or
e.g. by capturing the stdout output of subprocesses (redirecting it to
stderr is OK).

Tests based on ``qemu_test.QemuSystemTest`` can easily:

 * Customize the command line arguments given to the convenience
   ``self.vm`` attribute (a QEMUMachine instance)

 * Interact with the QEMU monitor, send QMP commands and check
   their results

 * Interact with the guest OS, using the convenience console device
   (which may be useful to assert the effectiveness and correctness of
   command line arguments or QMP commands)

 * Download (and cache) remote data files, such as firmware and kernel
   images

Running tests
-------------

You can run the functional tests simply by executing:

.. code::

  make check-functional

It is also possible to run tests for a certain target only, for example
the following line will only run the tests for the x86_64 target:

.. code::

  make check-functional-x86_64

To run a single test file without the meson test runner, you can also
execute the file directly by specifying two environment variables first,
the PYTHONPATH that has to include the python folder and the tests/functional
folder of the source tree, and QEMU_TEST_QEMU_BINARY that has to point
to the QEMU binary that should be used for the test. The current working
directory should be your build folder. For example::

  $ export PYTHONPATH=../python:../tests/functional
  $ export QEMU_TEST_QEMU_BINARY=$PWD/qemu-system-x86_64
  $ pyvenv/bin/python3 ../tests/functional/test_file.py

The test framework will automatically purge any scratch files created during
the tests. If needing to debug a failed test, it is possible to keep these
files around on disk by setting ```QEMU_TEST_KEEP_SCRATCH=1``` as an env
variable.  Any preserved files will be deleted the next time the test is run
without this variable set.

Logging
-------

The framework collects log files for each test in the build directory
in the following subfolder::

 <builddir>/tests/functional/<arch>/<fileid>.<classid>.<testname>/

There are usually three log files:

* ``base.log`` contains the generic logging information that is written
  by the calls to the logging functions in the test code (e.g. by calling
  the ``self.log.info()`` or ``self.log.debug()`` functions).
* ``console.log`` contains the output of the serial console of the guest.
* ``default.log`` contains the output of QEMU. This file could be named
  differently if the test chooses to use a different identifier for
  the guest VM (e.g. when the test spins up multiple VMs).

Introduction to writing tests
-----------------------------

The ``tests/functional/qemu_test`` directory provides the ``qemu_test``
Python module, containing the ``qemu_test.QemuSystemTest`` class.
Here is a simple usage example:

.. code::

  #!/usr/bin/env python3

  from qemu_test import QemuSystemTest

  class Version(QemuSystemTest):

      def test_qmp_human_info_version(self):
          self.vm.launch()
          res = self.vm.cmd('human-monitor-command',
                            command_line='info version')
          self.assertRegex(res, r'^(\d+\.\d+\.\d)')

  if __name__ == '__main__':
      QemuSystemTest.main()

By providing the "hash bang" line at the beginning of the script, marking
the file as executable and by calling into QemuSystemTest.main(), the test
can also be run stand-alone, without a test runner. OTOH when run via a test
runner, the QemuSystemTest.main() function takes care of running the test
functions in the right fassion (e.g. with TAP output that is required by the
meson test runner).

The ``qemu_test.QemuSystemTest`` base test class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``qemu_test.QemuSystemTest`` class has a number of characteristics
that are worth being mentioned.

First of all, it attempts to give each test a ready to use QEMUMachine
instance, available at ``self.vm``.  Because many tests will tweak the
QEMU command line, launching the QEMUMachine (by using ``self.vm.launch()``)
is left to the test writer.

The base test class has also support for tests with more than one
QEMUMachine. The way to get machines is through the ``self.get_vm()``
method which will return a QEMUMachine instance. The ``self.get_vm()``
method accepts arguments that will be passed to the QEMUMachine creation
and also an optional ``name`` attribute so you can identify a specific
machine and get it more than once through the tests methods. A simple
and hypothetical example follows:

.. code::

  from qemu_test import QemuSystemTest

  class MultipleMachines(QemuSystemTest):
      def test_multiple_machines(self):
          first_machine = self.get_vm()
          second_machine = self.get_vm()
          self.get_vm(name='third_machine').launch()

          first_machine.launch()
          second_machine.launch()

          first_res = first_machine.cmd(
              'human-monitor-command',
              command_line='info version')

          second_res = second_machine.cmd(
              'human-monitor-command',
              command_line='info version')

          third_res = self.get_vm(name='third_machine').cmd(
              'human-monitor-command',
              command_line='info version')

          self.assertEqual(first_res, second_res, third_res)

At test "tear down", ``qemu_test.QemuSystemTest`` handles all the QEMUMachines
shutdown.

QEMUMachine
-----------

The QEMUMachine API is already widely used in the Python iotests,
device-crash-test and other Python scripts.  It's a wrapper around the
execution of a QEMU binary, giving its users:

 * the ability to set command line arguments to be given to the QEMU
   binary

 * a ready to use QMP connection and interface, which can be used to
   send commands and inspect its results, as well as asynchronous
   events

 * convenience methods to set commonly used command line arguments in
   a more succinct and intuitive way

QEMU binary selection
^^^^^^^^^^^^^^^^^^^^^

The QEMU binary used for the ``self.vm`` QEMUMachine instance will
primarily depend on the value of the ``qemu_bin`` instance attribute.
If it is not explicitly set by the test code, its default value will
be the result the QEMU_TEST_QEMU_BINARY environment variable.

Debugging hung QEMU
^^^^^^^^^^^^^^^^^^^

When test cases go wrong it may be helpful to debug a stalled QEMU
process. While the QEMUMachine class owns the primary QMP monitor
socket, it is possible to request a second QMP monitor be created
by setting the ``QEMU_TEST_QMP_BACKDOOR`` env variable to refer
to a UNIX socket name. The ``qmp-shell`` command can then be
attached to the stalled QEMU to examine its live state.

Attribute reference
-------------------

QemuBaseTest
^^^^^^^^^^^^

The following attributes are available on any ``qemu_test.QemuBaseTest``
instance.

arch
""""

The target architecture of the QEMU binary.

Tests are also free to use this attribute value, for their own needs.
A test may, for instance, use this value when selecting the architecture
of a kernel or disk image to boot a VM with.

qemu_bin
""""""""

The preserved value of the ``QEMU_TEST_QEMU_BINARY`` environment
variable.

QemuUserTest
^^^^^^^^^^^^

The QemuUserTest class can be used for running an executable via the
usermode emulation binaries.

QemuSystemTest
^^^^^^^^^^^^^^

The QemuSystemTest class can be used for running tests via one of the
qemu-system-* binaries.

vm
""

A QEMUMachine instance, initially configured according to the given
``qemu_bin`` parameter.

cpu
"""

The cpu model that will be set to all QEMUMachine instances created
by the test.

machine
"""""""

The machine type that will be set to all QEMUMachine instances created
by the test. By using the set_machine() function of the QemuSystemTest
class to set this attribute, you can automatically check whether the
machine is available to skip the test in case it is not built into the
QEMU binary.

Asset handling
--------------

Many functional tests download assets (e.g. Linux kernels, initrds,
firmware images, etc.) from the internet to be able to run tests with
them. This imposes additional challenges to the test framework.

First there is the problem that some people might not have an
unconstrained internet connection, so such tests should not be run by
default when running ``make check``. To accomplish this situation,
the tests that download files should only be added to the "thorough"
speed mode in the meson.build file, while the "quick" speed mode is
fine for functional tests that can be run without downloading files.
``make check`` then only runs the quick functional tests along with
the other quick tests from the other test suites. If you choose to
run only run ``make check-functional``, the "thorough" tests will be
executed, too. And to run all functional tests along with the others,
you can use something like::

  make -j$(nproc) check SPEED=thorough

The second problem with downloading files from the internet are time
constraints. The time for downloading files should not be taken into
account when the test is running and the timeout of the test is ticking
(since downloading can be very slow, depending on the network bandwidth).
This problem is solved by downloading the assets ahead of time, before
the tests are run. This pre-caching is done with the qemu_test.Asset
class. To use it in your test, declare an asset in your test class with
its URL and SHA256 checksum like this::

    from qemu_test import Asset

    ASSET_somename = Asset(
        ('https://www.qemu.org/assets/images/qemu_head_200.png'),
        '34b74cad46ea28a2966c1d04e102510daf1fd73e6582b6b74523940d5da029dd')

In your test function, you can then get the file name of the cached
asset like this::

    def test_function(self):
        file_path = self.ASSET_somename.fetch()

The pre-caching will be done automatically when running
``make check-functional`` (but not when running e.g.
``make check-functional-<target>``). In case you just want to download
the assets without running the tests, you can do so by running::

    make precache-functional

The cache is populated in the ``~/.cache/qemu/download`` directory by
default, but the location can be changed by setting the
``QEMU_TEST_CACHE_DIR`` environment variable.

Skipping tests
--------------

Since the test framework is based on the common Python unittest framework,
you can use the usual Python decorators which allow for easily skipping
tests running under certain conditions, for example, on the lack of a binary
on the test system or when the running environment is a CI system. For further
information about those decorators, please refer to:

  https://docs.python.org/3/library/unittest.html#skipping-tests-and-expected-failures

While the conditions for skipping tests are often specifics of each one, there
are recurring scenarios identified by the QEMU developers and the use of
environment variables became a kind of standard way to enable/disable tests.

Here is a list of the most used variables:

QEMU_TEST_ALLOW_LARGE_STORAGE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Tests which are going to fetch or produce assets considered *large* are not
going to run unless that ``QEMU_TEST_ALLOW_LARGE_STORAGE=1`` is exported on
the environment.

The definition of *large* is a bit arbitrary here, but it usually means an
asset which occupies at least 1GB of size on disk when uncompressed.

QEMU_TEST_ALLOW_UNTRUSTED_CODE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
There are tests which will boot a kernel image or firmware that can be
considered not safe to run on the developer's workstation, thus they are
skipped by default. The definition of *not safe* is also arbitrary but
usually it means a blob which either its source or build process aren't
public available.

You should export ``QEMU_TEST_ALLOW_UNTRUSTED_CODE=1`` on the environment in
order to allow tests which make use of those kind of assets.

QEMU_TEST_FLAKY_TESTS
^^^^^^^^^^^^^^^^^^^^^
Some tests are not working reliably and thus are disabled by default.
This includes tests that don't run reliably on GitLab's CI which
usually expose real issues that are rarely seen on developer machines
due to the constraints of the CI environment. If you encounter a
similar situation then raise a bug and then mark the test as shown on
the code snippet below:

.. code::

  # See https://gitlab.com/qemu-project/qemu/-/issues/nnnn
  @skipUnless(os.getenv('QEMU_TEST_FLAKY_TESTS'), 'Test is unstable on GitLab')
  def test(self):
      do_something()

Tests should not live in this state forever and should either be fixed
or eventually removed.

QEMU_TEST_ALLOW_SLOW
^^^^^^^^^^^^^^^^^^^^
Tests that have a very long runtime and might run into timeout issues
e.g. if the QEMU binary has been compiled with debugging options enabled.
To avoid these timeout issues by default and to save some precious CPU
cycles during normal testing, such tests are disabled by default unless
the QEMU_TEST_ALLOW_SLOW environment variable has been set.


.. _unittest: https://docs.python.org/3/library/unittest.html
