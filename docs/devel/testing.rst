===============
Testing in QEMU
===============

This document describes the testing infrastructure in QEMU.

Testing with "make check"
=========================

The "make check" testing family includes most of the C based tests in QEMU. For
a quick help, run ``make check-help`` from the source tree.

The usual way to run these tests is:

.. code::

  make check

which includes QAPI schema tests, unit tests, and QTests. Different sub-types
of "make check" tests will be explained below.

Before running tests, it is best to build QEMU programs first. Some tests
expect the executables to exist and will fail with obscure messages if they
cannot find them.

Unit tests
----------

Unit tests, which can be invoked with ``make check-unit``, are simple C tests
that typically link to individual QEMU object files and exercise them by
calling exported functions.

If you are writing new code in QEMU, consider adding a unit test, especially
for utility modules that are relatively stateless or have few dependencies. To
add a new unit test:

1. Create a new source file. For example, ``tests/foo-test.c``.

2. Write the test. Normally you would include the header file which exports
   the module API, then verify the interface behaves as expected from your
   test. The test code should be organized with the glib testing framework.
   Copying and modifying an existing test is usually a good idea.

3. Add the test to ``tests/Makefile.include``. First, name the unit test
   program and add it to ``$(check-unit-y)``; then add a rule to build the
   executable.  For example:

.. code::

  check-unit-y += tests/foo-test$(EXESUF)
  tests/foo-test$(EXESUF): tests/foo-test.o $(test-util-obj-y)
  ...

Since unit tests don't require environment variables, the simplest way to debug
a unit test failure is often directly invoking it or even running it under
``gdb``. However there can still be differences in behavior between ``make``
invocations and your manual run, due to ``$MALLOC_PERTURB_`` environment
variable (which affects memory reclamation and catches invalid pointers better)
and gtester options. If necessary, you can run

.. code::

  make check-unit V=1

and copy the actual command line which executes the unit test, then run
it from the command line.

QTest
-----

QTest is a device emulation testing framework.  It can be very useful to test
device models; it could also control certain aspects of QEMU (such as virtual
clock stepping), with a special purpose "qtest" protocol.  Refer to the
documentation in ``qtest.c`` for more details of the protocol.

QTest cases can be executed with

.. code::

   make check-qtest

The QTest library is implemented by ``tests/libqtest.c`` and the API is defined
in ``tests/libqtest.h``.

Consider adding a new QTest case when you are introducing a new virtual
hardware, or extending one if you are adding functionalities to an existing
virtual device.

On top of libqtest, a higher level library, ``libqos``, was created to
encapsulate common tasks of device drivers, such as memory management and
communicating with system buses or devices. Many virtual device tests use
libqos instead of directly calling into libqtest.

Steps to add a new QTest case are:

1. Create a new source file for the test. (More than one file can be added as
   necessary.) For example, ``tests/test-foo-device.c``.

2. Write the test code with the glib and libqtest/libqos API. See also existing
   tests and the library headers for reference.

3. Register the new test in ``tests/Makefile.include``. Add the test executable
   name to an appropriate ``check-qtest-*-y`` variable. For example:

   ``check-qtest-generic-y = tests/test-foo-device$(EXESUF)``

4. Add object dependencies of the executable in the Makefile, including the
   test source file(s) and other interesting objects. For example:

   ``tests/test-foo-device$(EXESUF): tests/test-foo-device.o $(libqos-obj-y)``

Debugging a QTest failure is slightly harder than the unit test because the
tests look up QEMU program names in the environment variables, such as
``QTEST_QEMU_BINARY`` and ``QTEST_QEMU_IMG``, and also because it is not easy
to attach gdb to the QEMU process spawned from the test. But manual invoking
and using gdb on the test is still simple to do: find out the actual command
from the output of

.. code::

  make check-qtest V=1

which you can run manually.

QAPI schema tests
-----------------

The QAPI schema tests validate the QAPI parser used by QMP, by feeding
predefined input to the parser and comparing the result with the reference
output.

The input/output data is managed under the ``tests/qapi-schema`` directory.
Each test case includes four files that have a common base name:

  * ``${casename}.json`` - the file contains the JSON input for feeding the
    parser
  * ``${casename}.out`` - the file contains the expected stdout from the parser
  * ``${casename}.err`` - the file contains the expected stderr from the parser
  * ``${casename}.exit`` - the expected error code

Consider adding a new QAPI schema test when you are making a change on the QAPI
parser (either fixing a bug or extending/modifying the syntax). To do this:

1. Add four files for the new case as explained above. For example:

  ``$EDITOR tests/qapi-schema/foo.{json,out,err,exit}``.

2. Add the new test in ``tests/Makefile.include``. For example:

  ``qapi-schema += foo.json``

check-block
-----------

``make check-block`` is a legacy command to invoke block layer iotests and is
rarely used. See "QEMU iotests" section below for more information.

GCC gcov support
----------------

``gcov`` is a GCC tool to analyze the testing coverage by
instrumenting the tested code. To use it, configure QEMU with
``--enable-gcov`` option and build. Then run ``make check`` as usual.

If you want to gather coverage information on a single test the ``make
clean-coverage`` target can be used to delete any existing coverage
information before running a single test.

You can generate a HTML coverage report by executing ``make
coverage-report`` which will create
./reports/coverage/coverage-report.html. If you want to create it
elsewhere simply execute ``make /foo/bar/baz/coverage-report.html``.

Further analysis can be conducted by running the ``gcov`` command
directly on the various .gcda output files. Please read the ``gcov``
documentation for more information.

QEMU iotests
============

QEMU iotests, under the directory ``tests/qemu-iotests``, is the testing
framework widely used to test block layer related features. It is higher level
than "make check" tests and 99% of the code is written in bash or Python
scripts.  The testing success criteria is golden output comparison, and the
test files are named with numbers.

To run iotests, make sure QEMU is built successfully, then switch to the
``tests/qemu-iotests`` directory under the build directory, and run ``./check``
with desired arguments from there.

By default, "raw" format and "file" protocol is used; all tests will be
executed, except the unsupported ones. You can override the format and protocol
with arguments:

.. code::

  # test with qcow2 format
  ./check -qcow2
  # or test a different protocol
  ./check -nbd

It's also possible to list test numbers explicitly:

.. code::

  # run selected cases with qcow2 format
  ./check -qcow2 001 030 153

Cache mode can be selected with the "-c" option, which may help reveal bugs
that are specific to certain cache mode.

More options are supported by the ``./check`` script, run ``./check -h`` for
help.

Writing a new test case
-----------------------

Consider writing a tests case when you are making any changes to the block
layer. An iotest case is usually the choice for that. There are already many
test cases, so it is possible that extending one of them may achieve the goal
and save the boilerplate to create one.  (Unfortunately, there isn't a 100%
reliable way to find a related one out of hundreds of tests.  One approach is
using ``git grep``.)

Usually an iotest case consists of two files. One is an executable that
produces output to stdout and stderr, the other is the expected reference
output. They are given the same number in file names. E.g. Test script ``055``
and reference output ``055.out``.

In rare cases, when outputs differ between cache mode ``none`` and others, a
``.out.nocache`` file is added. In other cases, when outputs differ between
image formats, more than one ``.out`` files are created ending with the
respective format names, e.g. ``178.out.qcow2`` and ``178.out.raw``.

There isn't a hard rule about how to write a test script, but a new test is
usually a (copy and) modification of an existing case.  There are a few
commonly used ways to create a test:

* A Bash script. It will make use of several environmental variables related
  to the testing procedure, and could source a group of ``common.*`` libraries
  for some common helper routines.

* A Python unittest script. Import ``iotests`` and create a subclass of
  ``iotests.QMPTestCase``, then call ``iotests.main`` method. The downside of
  this approach is that the output is too scarce, and the script is considered
  harder to debug.

* A simple Python script without using unittest module. This could also import
  ``iotests`` for launching QEMU and utilities etc, but it doesn't inherit
  from ``iotests.QMPTestCase`` therefore doesn't use the Python unittest
  execution. This is a combination of 1 and 2.

Pick the language per your preference since both Bash and Python have
comparable library support for invoking and interacting with QEMU programs. If
you opt for Python, it is strongly recommended to write Python 3 compatible
code.

Both Python and Bash frameworks in iotests provide helpers to manage test
images. They can be used to create and clean up images under the test
directory. If no I/O or any protocol specific feature is needed, it is often
more convenient to use the pseudo block driver, ``null-co://``, as the test
image, which doesn't require image creation or cleaning up. Avoid system-wide
devices or files whenever possible, such as ``/dev/null`` or ``/dev/zero``.
Otherwise, image locking implications have to be considered.  For example,
another application on the host may have locked the file, possibly leading to a
test failure.  If using such devices are explicitly desired, consider adding
``locking=off`` option to disable image locking.

Docker based tests
==================

Introduction
------------

The Docker testing framework in QEMU utilizes public Docker images to build and
test QEMU in predefined and widely accessible Linux environments.  This makes
it possible to expand the test coverage across distros, toolchain flavors and
library versions.

Prerequisites
-------------

Install "docker" with the system package manager and start the Docker service
on your development machine, then make sure you have the privilege to run
Docker commands. Typically it means setting up passwordless ``sudo docker``
command or login as root. For example:

.. code::

  $ sudo yum install docker
  $ # or `apt-get install docker` for Ubuntu, etc.
  $ sudo systemctl start docker
  $ sudo docker ps

The last command should print an empty table, to verify the system is ready.

An alternative method to set up permissions is by adding the current user to
"docker" group and making the docker daemon socket file (by default
``/var/run/docker.sock``) accessible to the group:

.. code::

  $ sudo groupadd docker
  $ sudo usermod $USER -a -G docker
  $ sudo chown :docker /var/run/docker.sock

Note that any one of above configurations makes it possible for the user to
exploit the whole host with Docker bind mounting or other privileged
operations.  So only do it on development machines.

Quickstart
----------

From source tree, type ``make docker`` to see the help. Testing can be started
without configuring or building QEMU (``configure`` and ``make`` are done in
the container, with parameters defined by the make target):

.. code::

  make docker-test-build@min-glib

This will create a container instance using the ``min-glib`` image (the image
is downloaded and initialized automatically), in which the ``test-build`` job
is executed.

Images
------

Along with many other images, the ``min-glib`` image is defined in a Dockerfile
in ``tests/docker/dockefiles/``, called ``min-glib.docker``. ``make docker``
command will list all the available images.

To add a new image, simply create a new ``.docker`` file under the
``tests/docker/dockerfiles/`` directory.

A ``.pre`` script can be added beside the ``.docker`` file, which will be
executed before building the image under the build context directory. This is
mainly used to do necessary host side setup. One such setup is ``binfmt_misc``,
for example, to make qemu-user powered cross build containers work.

Tests
-----

Different tests are added to cover various configurations to build and test
QEMU.  Docker tests are the executables under ``tests/docker`` named
``test-*``. They are typically shell scripts and are built on top of a shell
library, ``tests/docker/common.rc``, which provides helpers to find the QEMU
source and build it.

The full list of tests is printed in the ``make docker`` help.

Tools
-----

There are executables that are created to run in a specific Docker environment.
This makes it easy to write scripts that have heavy or special dependencies,
but are still very easy to use.

Currently the only tool is ``travis``, which mimics the Travis-CI tests in a
container. It runs in the ``travis`` image:

.. code::

  make docker-travis@travis

Debugging a Docker test failure
-------------------------------

When CI tasks, maintainers or yourself report a Docker test failure, follow the
below steps to debug it:

1. Locally reproduce the failure with the reported command line. E.g. run
   ``make docker-test-mingw@fedora J=8``.
2. Add "V=1" to the command line, try again, to see the verbose output.
3. Further add "DEBUG=1" to the command line. This will pause in a shell prompt
   in the container right before testing starts. You could either manually
   build QEMU and run tests from there, or press Ctrl-D to let the Docker
   testing continue.
4. If you press Ctrl-D, the same building and testing procedure will begin, and
   will hopefully run into the error again. After that, you will be dropped to
   the prompt for debug.

Options
-------

Various options can be used to affect how Docker tests are done. The full
list is in the ``make docker`` help text. The frequently used ones are:

* ``V=1``: the same as in top level ``make``. It will be propagated to the
  container and enable verbose output.
* ``J=$N``: the number of parallel tasks in make commands in the container,
  similar to the ``-j $N`` option in top level ``make``. (The ``-j`` option in
  top level ``make`` will not be propagated into the container.)
* ``DEBUG=1``: enables debug. See the previous "Debugging a Docker test
  failure" section.

VM testing
==========

This test suite contains scripts that bootstrap various guest images that have
necessary packages to build QEMU. The basic usage is documented in ``Makefile``
help which is displayed with ``make vm-test``.

Quickstart
----------

Run ``make vm-test`` to list available make targets. Invoke a specific make
command to run build test in an image. For example, ``make vm-build-freebsd``
will build the source tree in the FreeBSD image. The command can be executed
from either the source tree or the build dir; if the former, ``./configure`` is
not needed. The command will then generate the test image in ``./tests/vm/``
under the working directory.

Note: images created by the scripts accept a well-known RSA key pair for SSH
access, so they SHOULD NOT be exposed to external interfaces if you are
concerned about attackers taking control of the guest and potentially
exploiting a QEMU security bug to compromise the host.

QEMU binary
-----------

By default, qemu-system-x86_64 is searched in $PATH to run the guest. If there
isn't one, or if it is older than 2.10, the test won't work. In this case,
provide the QEMU binary in env var: ``QEMU=/path/to/qemu-2.10+``.

Make jobs
---------

The ``-j$X`` option in the make command line is not propagated into the VM,
specify ``J=$X`` to control the make jobs in the guest.

Debugging
---------

Add ``DEBUG=1`` and/or ``V=1`` to the make command to allow interactive
debugging and verbose output. If this is not enough, see the next section.
``V=1`` will be propagated down into the make jobs in the guest.

Manual invocation
-----------------

Each guest script is an executable script with the same command line options.
For example to work with the netbsd guest, use ``$QEMU_SRC/tests/vm/netbsd``:

.. code::

    $ cd $QEMU_SRC/tests/vm

    # To bootstrap the image
    $ ./netbsd --build-image --image /var/tmp/netbsd.img
    <...>

    # To run an arbitrary command in guest (the output will not be echoed unless
    # --debug is added)
    $ ./netbsd --debug --image /var/tmp/netbsd.img uname -a

    # To build QEMU in guest
    $ ./netbsd --debug --image /var/tmp/netbsd.img --build-qemu $QEMU_SRC

    # To get to an interactive shell
    $ ./netbsd --interactive --image /var/tmp/netbsd.img sh

Adding new guests
-----------------

Please look at existing guest scripts for how to add new guests.

Most importantly, create a subclass of BaseVM and implement ``build_image()``
method and define ``BUILD_SCRIPT``, then finally call ``basevm.main()`` from
the script's ``main()``.

* Usually in ``build_image()``, a template image is downloaded from a
  predefined URL. ``BaseVM._download_with_cache()`` takes care of the cache and
  the checksum, so consider using it.

* Once the image is downloaded, users, SSH server and QEMU build deps should
  be set up:

  - Root password set to ``BaseVM.ROOT_PASS``
  - User ``BaseVM.GUEST_USER`` is created, and password set to
    ``BaseVM.GUEST_PASS``
  - SSH service is enabled and started on boot,
    ``$QEMU_SRC/tests/keys/id_rsa.pub`` is added to ssh's ``authorized_keys``
    file of both root and the normal user
  - DHCP client service is enabled and started on boot, so that it can
    automatically configure the virtio-net-pci NIC and communicate with QEMU
    user net (10.0.2.2)
  - Necessary packages are installed to untar the source tarball and build
    QEMU

* Write a proper ``BUILD_SCRIPT`` template, which should be a shell script that
  untars a raw virtio-blk block device, which is the tarball data blob of the
  QEMU source tree, then configure/build it. Running "make check" is also
  recommended.

Image fuzzer testing
====================

An image fuzzer was added to exercise format drivers. Currently only qcow2 is
supported. To start the fuzzer, run

.. code::

  tests/image-fuzzer/runner.py -c '[["qemu-img", "info", "$test_img"]]' /tmp/test qcow2

Alternatively, some command different from "qemu-img info" can be tested, by
changing the ``-c`` option.

Acceptance tests using the Avocado Framework
============================================

The ``tests/acceptance`` directory hosts functional tests, also known
as acceptance level tests.  They're usually higher level tests, and
may interact with external resources and with various guest operating
systems.

These tests are written using the Avocado Testing Framework (which must
be installed separately) in conjunction with a the ``avocado_qemu.Test``
class, implemented at ``tests/acceptance/avocado_qemu``.

Tests based on ``avocado_qemu.Test`` can easily:

 * Customize the command line arguments given to the convenience
   ``self.vm`` attribute (a QEMUMachine instance)

 * Interact with the QEMU monitor, send QMP commands and check
   their results

 * Interact with the guest OS, using the convenience console device
   (which may be useful to assert the effectiveness and correctness of
   command line arguments or QMP commands)

 * Interact with external data files that accompany the test itself
   (see ``self.get_data()``)

 * Download (and cache) remote data files, such as firmware and kernel
   images

 * Have access to a library of guest OS images (by means of the
   ``avocado.utils.vmimage`` library)

 * Make use of various other test related utilities available at the
   test class itself and at the utility library:

   - http://avocado-framework.readthedocs.io/en/latest/api/test/avocado.html#avocado.Test
   - http://avocado-framework.readthedocs.io/en/latest/api/utils/avocado.utils.html

Running tests
-------------

You can run the acceptance tests simply by executing:

.. code::

  make check-acceptance

This involves the automatic creation of Python virtual environment
within the build tree (at ``tests/venv``) which will have all the
right dependencies, and will save tests results also within the
build tree (at ``tests/results``).

Note: the build environment must be using a Python 3 stack, and have
the ``venv`` and ``pip`` packages installed.  If necessary, make sure
``configure`` is called with ``--python=`` and that those modules are
available.  On Debian and Ubuntu based systems, depending on the
specific version, they may be on packages named ``python3-venv`` and
``python3-pip``.

The scripts installed inside the virtual environment may be used
without an "activation".  For instance, the Avocado test runner
may be invoked by running:

 .. code::

  tests/venv/bin/avocado run $OPTION1 $OPTION2 tests/acceptance/

Manual Installation
-------------------

To manually install Avocado and its dependencies, run:

.. code::

  pip install --user avocado-framework

Alternatively, follow the instructions on this link:

  http://avocado-framework.readthedocs.io/en/latest/GetStartedGuide.html#installing-avocado

Overview
--------

The ``tests/acceptance/avocado_qemu`` directory provides the
``avocado_qemu`` Python module, containing the ``avocado_qemu.Test``
class.  Here's a simple usage example:

.. code::

  from avocado_qemu import Test


  class Version(Test):
      """
      :avocado: tags=quick
      """
      def test_qmp_human_info_version(self):
          self.vm.launch()
          res = self.vm.command('human-monitor-command',
                                command_line='info version')
          self.assertRegexpMatches(res, r'^(\d+\.\d+\.\d)')

To execute your test, run:

.. code::

  avocado run version.py

Tests may be classified according to a convention by using docstring
directives such as ``:avocado: tags=TAG1,TAG2``.  To run all tests
in the current directory, tagged as "quick", run:

.. code::

  avocado run -t quick .

The ``avocado_qemu.Test`` base test class
-----------------------------------------

The ``avocado_qemu.Test`` class has a number of characteristics that
are worth being mentioned right away.

First of all, it attempts to give each test a ready to use QEMUMachine
instance, available at ``self.vm``.  Because many tests will tweak the
QEMU command line, launching the QEMUMachine (by using ``self.vm.launch()``)
is left to the test writer.

The base test class has also support for tests with more than one
QEMUMachine. The way to get machines is through the ``self.get_vm()``
method which will return a QEMUMachine instance. The ``self.get_vm()``
method accepts arguments that will be passed to the QEMUMachine creation
and also an optional `name` attribute so you can identify a specific
machine and get it more than once through the tests methods. A simple
and hypothetical example follows:

.. code::

  from avocado_qemu import Test


  class MultipleMachines(Test):
      """
      :avocado: enable
      """
      def test_multiple_machines(self):
          first_machine = self.get_vm()
          second_machine = self.get_vm()
          self.get_vm(name='third_machine').launch()

          first_machine.launch()
          second_machine.launch()

          first_res = first_machine.command(
              'human-monitor-command',
              command_line='info version')

          second_res = second_machine.command(
              'human-monitor-command',
              command_line='info version')

          third_res = self.get_vm(name='third_machine').command(
              'human-monitor-command',
              command_line='info version')

          self.assertEquals(first_res, second_res, third_res)

At test "tear down", ``avocado_qemu.Test`` handles all the QEMUMachines
shutdown.

QEMUMachine
~~~~~~~~~~~

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
~~~~~~~~~~~~~~~~~~~~~

The QEMU binary used for the ``self.vm`` QEMUMachine instance will
primarily depend on the value of the ``qemu_bin`` parameter.  If it's
not explicitly set, its default value will be the result of a dynamic
probe in the same source tree.  A suitable binary will be one that
targets the architecture matching host machine.

Based on this description, test writers will usually rely on one of
the following approaches:

1) Set ``qemu_bin``, and use the given binary

2) Do not set ``qemu_bin``, and use a QEMU binary named like
   "${arch}-softmmu/qemu-system-${arch}", either in the current
   working directory, or in the current source tree.

The resulting ``qemu_bin`` value will be preserved in the
``avocado_qemu.Test`` as an attribute with the same name.

Attribute reference
-------------------

Besides the attributes and methods that are part of the base
``avocado.Test`` class, the following attributes are available on any
``avocado_qemu.Test`` instance.

vm
~~

A QEMUMachine instance, initially configured according to the given
``qemu_bin`` parameter.

arch
~~~~

The architecture can be used on different levels of the stack, e.g. by
the framework or by the test itself.  At the framework level, it will
currently influence the selection of a QEMU binary (when one is not
explicitly given).

Tests are also free to use this attribute value, for their own needs.
A test may, for instance, use the same value when selecting the
architecture of a kernel or disk image to boot a VM with.

The ``arch`` attribute will be set to the test parameter of the same
name.  If one is not given explicitly, it will either be set to
``None``, or, if the test is tagged with one (and only one)
``:avocado: tags=arch:VALUE`` tag, it will be set to ``VALUE``.

qemu_bin
~~~~~~~~

The preserved value of the ``qemu_bin`` parameter or the result of the
dynamic probe for a QEMU binary in the current working directory or
source tree.

Parameter reference
-------------------

To understand how Avocado parameters are accessed by tests, and how
they can be passed to tests, please refer to::

  http://avocado-framework.readthedocs.io/en/latest/WritingTests.html#accessing-test-parameters

Parameter values can be easily seen in the log files, and will look
like the following:

.. code::

  PARAMS (key=qemu_bin, path=*, default=x86_64-softmmu/qemu-system-x86_64) => 'x86_64-softmmu/qemu-system-x86_64

arch
~~~~

The architecture that will influence the selection of a QEMU binary
(when one is not explicitly given).

Tests are also free to use this parameter value, for their own needs.
A test may, for instance, use the same value when selecting the
architecture of a kernel or disk image to boot a VM with.

This parameter has a direct relation with the ``arch`` attribute.  If
not given, it will default to None.

qemu_bin
~~~~~~~~

The exact QEMU binary to be used on QEMUMachine.

Uninstalling Avocado
--------------------

If you've followed the manual installation instructions above, you can
easily uninstall Avocado.  Start by listing the packages you have
installed::

  pip list --user

And remove any package you want with::

  pip uninstall <package_name>

If you've used ``make check-acceptance``, the Python virtual environment where
Avocado is installed will be cleaned up as part of ``make check-clean``.
