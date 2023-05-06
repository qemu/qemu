.. _testing:

Testing in QEMU
===============

This document describes the testing infrastructure in QEMU.

Testing with "make check"
-------------------------

The "make check" testing family includes most of the C based tests in QEMU. For
a quick help, run ``make check-help`` from the source tree.

The usual way to run these tests is:

.. code::

  make check

which includes QAPI schema tests, unit tests, QTests and some iotests.
Different sub-types of "make check" tests will be explained below.

Before running tests, it is best to build QEMU programs first. Some tests
expect the executables to exist and will fail with obscure messages if they
cannot find them.

Unit tests
~~~~~~~~~~

Unit tests, which can be invoked with ``make check-unit``, are simple C tests
that typically link to individual QEMU object files and exercise them by
calling exported functions.

If you are writing new code in QEMU, consider adding a unit test, especially
for utility modules that are relatively stateless or have few dependencies. To
add a new unit test:

1. Create a new source file. For example, ``tests/unit/foo-test.c``.

2. Write the test. Normally you would include the header file which exports
   the module API, then verify the interface behaves as expected from your
   test. The test code should be organized with the glib testing framework.
   Copying and modifying an existing test is usually a good idea.

3. Add the test to ``tests/unit/meson.build``. The unit tests are listed in a
   dictionary called ``tests``.  The values are any additional sources and
   dependencies to be linked with the test.  For a simple test whose source
   is in ``tests/unit/foo-test.c``, it is enough to add an entry like::

     {
       ...
       'foo-test': [],
       ...
     }

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
~~~~~

QTest is a device emulation testing framework.  It can be very useful to test
device models; it could also control certain aspects of QEMU (such as virtual
clock stepping), with a special purpose "qtest" protocol.  Refer to
:doc:`qtest` for more details.

QTest cases can be executed with

.. code::

   make check-qtest

Writing portable test cases
~~~~~~~~~~~~~~~~~~~~~~~~~~~
Both unit tests and qtests can run on POSIX hosts as well as Windows hosts.
Care must be taken when writing portable test cases that can be built and run
successfully on various hosts. The following list shows some best practices:

* Use portable APIs from glib whenever necessary, e.g.: g_setenv(),
  g_mkdtemp(), g_mkdir().
* Avoid using hardcoded /tmp for temporary file directory.
  Use g_get_tmp_dir() instead.
* Bear in mind that Windows has different special string representation for
  stdin/stdout/stderr and null devices. For example if your test case uses
  "/dev/fd/2" and "/dev/null" on Linux, remember to use "2" and "nul" on
  Windows instead. Also IO redirection does not work on Windows, so avoid
  using "2>nul" whenever necessary.
* If your test cases uses the blkdebug feature, use relative path to pass
  the config and image file paths in the command line as Windows absolute
  path contains the delimiter ":" which will confuse the blkdebug parser.
* Use double quotes in your extra QEMU command line in your test cases
  instead of single quotes, as Windows does not drop single quotes when
  passing the command line to QEMU.
* Windows opens a file in text mode by default, while a POSIX compliant
  implementation treats text files and binary files the same. So if your
  test cases opens a file to write some data and later wants to compare the
  written data with the original one, be sure to pass the letter 'b' as
  part of the mode string to fopen(), or O_BINARY flag for the open() call.
* If a certain test case can only run on POSIX or Linux hosts, use a proper
  #ifdef in the codes. If the whole test suite cannot run on Windows, disable
  the build in the meson.build file.

QAPI schema tests
~~~~~~~~~~~~~~~~~

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
~~~~~~~~~~~

``make check-block`` runs a subset of the block layer iotests (the tests that
are in the "auto" group).
See the "QEMU iotests" section below for more information.

QEMU iotests
------------

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
~~~~~~~~~~~~~~~~~~~~~~~

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

Debugging a test case
~~~~~~~~~~~~~~~~~~~~~

The following options to the ``check`` script can be useful when debugging
a failing test:

* ``-gdb`` wraps every QEMU invocation in a ``gdbserver``, which waits for a
  connection from a gdb client.  The options given to ``gdbserver`` (e.g. the
  address on which to listen for connections) are taken from the ``$GDB_OPTIONS``
  environment variable.  By default (if ``$GDB_OPTIONS`` is empty), it listens on
  ``localhost:12345``.
  It is possible to connect to it for example with
  ``gdb -iex "target remote $addr"``, where ``$addr`` is the address
  ``gdbserver`` listens on.
  If the ``-gdb`` option is not used, ``$GDB_OPTIONS`` is ignored,
  regardless of whether it is set or not.

* ``-valgrind`` attaches a valgrind instance to QEMU. If it detects
  warnings, it will print and save the log in
  ``$TEST_DIR/<valgrind_pid>.valgrind``.
  The final command line will be ``valgrind --log-file=$TEST_DIR/
  <valgrind_pid>.valgrind --error-exitcode=99 $QEMU ...``

* ``-d`` (debug) just increases the logging verbosity, showing
  for example the QMP commands and answers.

* ``-p`` (print) redirects QEMU’s stdout and stderr to the test output,
  instead of saving it into a log file in
  ``$TEST_DIR/qemu-machine-<random_string>``.

Test case groups
~~~~~~~~~~~~~~~~

"Tests may belong to one or more test groups, which are defined in the form
of a comment in the test source file. By convention, test groups are listed
in the second line of the test file, after the "#!/..." line, like this:

.. code::

  #!/usr/bin/env python3
  # group: auto quick
  #
  ...

Another way of defining groups is creating the tests/qemu-iotests/group.local
file. This should be used only for downstream (this file should never appear
in upstream). This file may be used for defining some downstream test groups
or for temporarily disabling tests, like this:

.. code::

  # groups for some company downstream process
  #
  # ci - tests to run on build
  # down - our downstream tests, not for upstream
  #
  # Format of each line is:
  # TEST_NAME TEST_GROUP [TEST_GROUP ]...

  013 ci
  210 disabled
  215 disabled
  our-ugly-workaround-test down ci

Note that the following group names have a special meaning:

- quick: Tests in this group should finish within a few seconds.

- auto: Tests in this group are used during "make check" and should be
  runnable in any case. That means they should run with every QEMU binary
  (also non-x86), with every QEMU configuration (i.e. must not fail if
  an optional feature is not compiled in - but reporting a "skip" is ok),
  work at least with the qcow2 file format, work with all kind of host
  filesystems and users (e.g. "nobody" or "root") and must not take too
  much memory and disk space (since CI pipelines tend to fail otherwise).

- disabled: Tests in this group are disabled and ignored by check.

.. _container-ref:

Container based tests
---------------------

Introduction
~~~~~~~~~~~~

The container testing framework in QEMU utilizes public images to
build and test QEMU in predefined and widely accessible Linux
environments. This makes it possible to expand the test coverage
across distros, toolchain flavors and library versions. The support
was originally written for Docker although we also support Podman as
an alternative container runtime. Although many of the target
names and scripts are prefixed with "docker" the system will
automatically run on whichever is configured.

The container images are also used to augment the generation of tests
for testing TCG. See :ref:`checktcg-ref` for more details.

Docker Prerequisites
~~~~~~~~~~~~~~~~~~~~

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

Podman Prerequisites
~~~~~~~~~~~~~~~~~~~~

Install "podman" with the system package manager.

.. code::

  $ sudo dnf install podman
  $ podman ps

The last command should print an empty table, to verify the system is ready.

Quickstart
~~~~~~~~~~

From source tree, type ``make docker-help`` to see the help. Testing
can be started without configuring or building QEMU (``configure`` and
``make`` are done in the container, with parameters defined by the
make target):

.. code::

  make docker-test-build@centos8

This will create a container instance using the ``centos8`` image (the image
is downloaded and initialized automatically), in which the ``test-build`` job
is executed.

Registry
~~~~~~~~

The QEMU project has a container registry hosted by GitLab at
``registry.gitlab.com/qemu-project/qemu`` which will automatically be
used to pull in pre-built layers. This avoids unnecessary strain on
the distro archives created by multiple developers running the same
container build steps over and over again. This can be overridden
locally by using the ``NOCACHE`` build option:

.. code::

   make docker-image-debian-arm64-cross NOCACHE=1

Images
~~~~~~

Along with many other images, the ``centos8`` image is defined in a Dockerfile
in ``tests/docker/dockerfiles/``, called ``centos8.docker``. ``make docker-help``
command will list all the available images.

A ``.pre`` script can be added beside the ``.docker`` file, which will be
executed before building the image under the build context directory. This is
mainly used to do necessary host side setup. One such setup is ``binfmt_misc``,
for example, to make qemu-user powered cross build containers work.

Most of the existing Dockerfiles were written by hand, simply by creating a
a new ``.docker`` file under the ``tests/docker/dockerfiles/`` directory.
This has led to an inconsistent set of packages being present across the
different containers.

Thus going forward, QEMU is aiming to automatically generate the Dockerfiles
using the ``lcitool`` program provided by the ``libvirt-ci`` project:

  https://gitlab.com/libvirt/libvirt-ci

``libvirt-ci`` contains an ``lcitool`` program as well as a list of
mappings to distribution package names for a wide variety of third
party projects.  ``lcitool`` applies the mappings to a list of build
pre-requisites in ``tests/lcitool/projects/qemu.yml``, determines the
list of native packages to install on each distribution, and uses them
to generate build environments (dockerfiles and Cirrus CI variable files)
that are consistent across OS distribution.


Adding new build pre-requisites
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When preparing a patch series that adds a new build
pre-requisite to QEMU, the prerequisites should to be added to
``tests/lcitool/projects/qemu.yml`` in order to make the dependency
available in the CI build environments.

In the simple case where the pre-requisite is already known to ``libvirt-ci``
the following steps are needed:

 * Edit ``tests/lcitool/projects/qemu.yml`` and add the pre-requisite

 * Run ``make lcitool-refresh`` to re-generate all relevant build environment
   manifests

It may be that ``libvirt-ci`` does not know about the new pre-requisite.
If that is the case, some extra preparation steps will be required
first to contribute the mapping to the ``libvirt-ci`` project:

 * Fork the ``libvirt-ci`` project on gitlab

 * Add an entry for the new build prerequisite to
   ``lcitool/facts/mappings.yml``, listing its native package name on as
   many OS distros as practical.  Run ``python -m pytest --regenerate-output``
   and check that the changes are correct.

 * Commit the ``mappings.yml`` change together with the regenerated test
   files, and submit a merge request to the ``libvirt-ci`` project.
   Please note in the description that this is a new build pre-requisite
   desired for use with QEMU.

 * CI pipeline will run to validate that the changes to ``mappings.yml``
   are correct, by attempting to install the newly listed package on
   all OS distributions supported by ``libvirt-ci``.

 * Once the merge request is accepted, go back to QEMU and update
   the ``tests/lcitool/libvirt-ci`` submodule to point to a commit that
   contains the ``mappings.yml`` update.  Then add the prerequisite and
   run ``make lcitool-refresh``.

 * Please also trigger gitlab container generation pipelines on your change
   for as many OS distros as practical to make sure that there are no
   obvious breakages when adding the new pre-requisite. Please see
   `CI <https://www.qemu.org/docs/master/devel/ci.html>`__ documentation
   page on how to trigger gitlab CI pipelines on your change.

For enterprise distros that default to old, end-of-life versions of the
Python runtime, QEMU uses a separate set of mappings that work with more
recent versions.  These can be found in ``tests/lcitool/mappings.yml``.
Modifying this file should not be necessary unless the new pre-requisite
is a Python library or tool.


Adding new OS distros
^^^^^^^^^^^^^^^^^^^^^

In some cases ``libvirt-ci`` will not know about the OS distro that is
desired to be tested. Before adding a new OS distro, discuss the proposed
addition:

 * Send a mail to qemu-devel, copying people listed in the
   MAINTAINERS file for ``Build and test automation``.

   There are limited CI compute resources available to QEMU, so the
   cost/benefit tradeoff of adding new OS distros needs to be considered.

 * File an issue at https://gitlab.com/libvirt/libvirt-ci/-/issues
   pointing to the qemu-devel mail thread in the archives.

   This alerts other people who might be interested in the work
   to avoid duplication, as well as to get feedback from libvirt-ci
   maintainers on any tips to ease the addition

Assuming there is agreement to add a new OS distro then

 * Fork the ``libvirt-ci`` project on gitlab

 * Add metadata under ``lcitool/facts/targets/`` for the new OS
   distro. There might be code changes required if the OS distro
   uses a package format not currently known. The ``libvirt-ci``
   maintainers can advise on this when the issue is filed.

 * Edit the ``lcitool/facts/mappings.yml`` change to add entries for
   the new OS, listing the native package names for as many packages
   as practical.  Run ``python -m pytest --regenerate-output`` and
   check that the changes are correct.

 * Commit the changes to ``lcitool/facts`` and the regenerated test
   files, and submit a merge request to the ``libvirt-ci`` project.
   Please note in the description that this is a new build pre-requisite
   desired for use with QEMU

 * CI pipeline will run to validate that the changes to ``mappings.yml``
   are correct, by attempting to install the newly listed package on
   all OS distributions supported by ``libvirt-ci``.

 * Once the merge request is accepted, go back to QEMU and update
   the ``libvirt-ci`` submodule to point to a commit that contains
   the ``mappings.yml`` update.


Tests
~~~~~

Different tests are added to cover various configurations to build and test
QEMU.  Docker tests are the executables under ``tests/docker`` named
``test-*``. They are typically shell scripts and are built on top of a shell
library, ``tests/docker/common.rc``, which provides helpers to find the QEMU
source and build it.

The full list of tests is printed in the ``make docker-help`` help.

Debugging a Docker test failure
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
~~~~~~~

Various options can be used to affect how Docker tests are done. The full
list is in the ``make docker`` help text. The frequently used ones are:

* ``V=1``: the same as in top level ``make``. It will be propagated to the
  container and enable verbose output.
* ``J=$N``: the number of parallel tasks in make commands in the container,
  similar to the ``-j $N`` option in top level ``make``. (The ``-j`` option in
  top level ``make`` will not be propagated into the container.)
* ``DEBUG=1``: enables debug. See the previous "Debugging a Docker test
  failure" section.

Thread Sanitizer
----------------

Thread Sanitizer (TSan) is a tool which can detect data races.  QEMU supports
building and testing with this tool.

For more information on TSan:

https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual

Thread Sanitizer in Docker
~~~~~~~~~~~~~~~~~~~~~~~~~~
TSan is currently supported in the ubuntu2204 docker.

The test-tsan test will build using TSan and then run make check.

.. code::

  make docker-test-tsan@ubuntu2204

TSan warnings under docker are placed in files located at build/tsan/.

We recommend using DEBUG=1 to allow launching the test from inside the docker,
and to allow review of the warnings generated by TSan.

Building and Testing with TSan
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It is possible to build and test with TSan, with a few additional steps.
These steps are normally done automatically in the docker.

There is a one time patch needed in clang-9 or clang-10 at this time:

.. code::

  sed -i 's/^const/static const/g' \
      /usr/lib/llvm-10/lib/clang/10.0.0/include/sanitizer/tsan_interface.h

To configure the build for TSan:

.. code::

  ../configure --enable-tsan --cc=clang-10 --cxx=clang++-10 \
               --disable-werror --extra-cflags="-O0"

The runtime behavior of TSAN is controlled by the TSAN_OPTIONS environment
variable.

More information on the TSAN_OPTIONS can be found here:

https://github.com/google/sanitizers/wiki/ThreadSanitizerFlags

For example:

.. code::

  export TSAN_OPTIONS=suppressions=<path to qemu>/tests/tsan/suppressions.tsan \
                      detect_deadlocks=false history_size=7 exitcode=0 \
                      log_path=<build path>/tsan/tsan_warning

The above exitcode=0 has TSan continue without error if any warnings are found.
This allows for running the test and then checking the warnings afterwards.
If you want TSan to stop and exit with error on warnings, use exitcode=66.

TSan Suppressions
~~~~~~~~~~~~~~~~~
Keep in mind that for any data race warning, although there might be a data race
detected by TSan, there might be no actual bug here.  TSan provides several
different mechanisms for suppressing warnings.  In general it is recommended
to fix the code if possible to eliminate the data race rather than suppress
the warning.

A few important files for suppressing warnings are:

tests/tsan/suppressions.tsan - Has TSan warnings we wish to suppress at runtime.
The comment on each suppression will typically indicate why we are
suppressing it.  More information on the file format can be found here:

https://github.com/google/sanitizers/wiki/ThreadSanitizerSuppressions

tests/tsan/blacklist.tsan - Has TSan warnings we wish to disable
at compile time for test or debug.
Add flags to configure to enable:

"--extra-cflags=-fsanitize-blacklist=<src path>/tests/tsan/blacklist.tsan"

More information on the file format can be found here under "Blacklist Format":

https://github.com/google/sanitizers/wiki/ThreadSanitizerFlags

TSan Annotations
~~~~~~~~~~~~~~~~
include/qemu/tsan.h defines annotations.  See this file for more descriptions
of the annotations themselves.  Annotations can be used to suppress
TSan warnings or give TSan more information so that it can detect proper
relationships between accesses of data.

Annotation examples can be found here:

https://github.com/llvm/llvm-project/tree/master/compiler-rt/test/tsan/

Good files to start with are: annotate_happens_before.cpp and ignore_race.cpp

The full set of annotations can be found here:

https://github.com/llvm/llvm-project/blob/master/compiler-rt/lib/tsan/rtl/tsan_interface_ann.cpp

docker-binfmt-image-debian-% targets
------------------------------------

It is possible to combine Debian's bootstrap scripts with a configured
``binfmt_misc`` to bootstrap a number of Debian's distros including
experimental ports not yet supported by a released OS. This can
simplify setting up a rootfs by using docker to contain the foreign
rootfs rather than manually invoking chroot.

Setting up ``binfmt_misc``
~~~~~~~~~~~~~~~~~~~~~~~~~~

You can use the script ``qemu-binfmt-conf.sh`` to configure a QEMU
user binary to automatically run binaries for the foreign
architecture. While the scripts will try their best to work with
dynamically linked QEMU's a statically linked one will present less
potential complications when copying into the docker image. Modern
kernels support the ``F`` (fix binary) flag which will open the QEMU
executable on setup and avoids the need to find and re-open in the
chroot environment. This is triggered with the ``--persistent`` flag.

Example invocation
~~~~~~~~~~~~~~~~~~

For example to setup the HPPA ports builds of Debian::

  make docker-binfmt-image-debian-sid-hppa \
    DEB_TYPE=sid DEB_ARCH=hppa \
    DEB_URL=http://ftp.ports.debian.org/debian-ports/ \
    DEB_KEYRING=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
    EXECUTABLE=(pwd)/qemu-hppa V=1

The ``DEB_`` variables are substitutions used by
``debian-boostrap.pre`` which is called to do the initial debootstrap
of the rootfs before it is copied into the container. The second stage
is run as part of the build. The final image will be tagged as
``qemu/debian-sid-hppa``.

VM testing
----------

This test suite contains scripts that bootstrap various guest images that have
necessary packages to build QEMU. The basic usage is documented in ``Makefile``
help which is displayed with ``make vm-help``.

Quickstart
~~~~~~~~~~

Run ``make vm-help`` to list available make targets. Invoke a specific make
command to run build test in an image. For example, ``make vm-build-freebsd``
will build the source tree in the FreeBSD image. The command can be executed
from either the source tree or the build dir; if the former, ``./configure`` is
not needed. The command will then generate the test image in ``./tests/vm/``
under the working directory.

Note: images created by the scripts accept a well-known RSA key pair for SSH
access, so they SHOULD NOT be exposed to external interfaces if you are
concerned about attackers taking control of the guest and potentially
exploiting a QEMU security bug to compromise the host.

QEMU binaries
~~~~~~~~~~~~~

By default, ``qemu-system-x86_64`` is searched in $PATH to run the guest. If
there isn't one, or if it is older than 2.10, the test won't work. In this case,
provide the QEMU binary in env var: ``QEMU=/path/to/qemu-2.10+``.

Likewise the path to ``qemu-img`` can be set in QEMU_IMG environment variable.

Make jobs
~~~~~~~~~

The ``-j$X`` option in the make command line is not propagated into the VM,
specify ``J=$X`` to control the make jobs in the guest.

Debugging
~~~~~~~~~

Add ``DEBUG=1`` and/or ``V=1`` to the make command to allow interactive
debugging and verbose output. If this is not enough, see the next section.
``V=1`` will be propagated down into the make jobs in the guest.

Manual invocation
~~~~~~~~~~~~~~~~~

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
~~~~~~~~~~~~~~~~~

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
--------------------

An image fuzzer was added to exercise format drivers. Currently only qcow2 is
supported. To start the fuzzer, run

.. code::

  tests/image-fuzzer/runner.py -c '[["qemu-img", "info", "$test_img"]]' /tmp/test qcow2

Alternatively, some command different from ``qemu-img info`` can be tested, by
changing the ``-c`` option.

Integration tests using the Avocado Framework
---------------------------------------------

The ``tests/avocado`` directory hosts integration tests. They're usually
higher level tests, and may interact with external resources and with
various guest operating systems.

These tests are written using the Avocado Testing Framework (which must
be installed separately) in conjunction with a the ``avocado_qemu.Test``
class, implemented at ``tests/avocado/avocado_qemu``.

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
~~~~~~~~~~~~~

You can run the avocado tests simply by executing:

.. code::

  make check-avocado

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

It is also possible to run tests based on tags using the
``make check-avocado`` command and the ``AVOCADO_TAGS`` environment
variable:

.. code::

   make check-avocado AVOCADO_TAGS=quick

Note that tags separated with commas have an AND behavior, while tags
separated by spaces have an OR behavior. For more information on Avocado
tags, see:

 https://avocado-framework.readthedocs.io/en/latest/guides/user/chapters/tags.html

To run a single test file, a couple of them, or a test within a file
using the ``make check-avocado`` command, set the ``AVOCADO_TESTS``
environment variable with the test files or test names. To run all
tests from a single file, use:

 .. code::

  make check-avocado AVOCADO_TESTS=$FILEPATH

The same is valid to run tests from multiple test files:

 .. code::

  make check-avocado AVOCADO_TESTS='$FILEPATH1 $FILEPATH2'

To run a single test within a file, use:

 .. code::

  make check-avocado AVOCADO_TESTS=$FILEPATH:$TESTCLASS.$TESTNAME

The same is valid to run single tests from multiple test files:

 .. code::

  make check-avocado AVOCADO_TESTS='$FILEPATH1:$TESTCLASS1.$TESTNAME1 $FILEPATH2:$TESTCLASS2.$TESTNAME2'

The scripts installed inside the virtual environment may be used
without an "activation".  For instance, the Avocado test runner
may be invoked by running:

 .. code::

  tests/venv/bin/avocado run $OPTION1 $OPTION2 tests/avocado/

Note that if ``make check-avocado`` was not executed before, it is
possible to create the Python virtual environment with the dependencies
needed running:

 .. code::

  make check-venv

It is also possible to run tests from a single file or a single test within
a test file. To run tests from a single file within the build tree, use:

 .. code::

  tests/venv/bin/avocado run tests/avocado/$TESTFILE

To run a single test within a test file, use:

 .. code::

  tests/venv/bin/avocado run tests/avocado/$TESTFILE:$TESTCLASS.$TESTNAME

Valid test names are visible in the output from any previous execution
of Avocado or ``make check-avocado``, and can also be queried using:

 .. code::

  tests/venv/bin/avocado list tests/avocado

Manual Installation
~~~~~~~~~~~~~~~~~~~

To manually install Avocado and its dependencies, run:

.. code::

  pip install --user avocado-framework

Alternatively, follow the instructions on this link:

  https://avocado-framework.readthedocs.io/en/latest/guides/user/chapters/installing.html

Overview
~~~~~~~~

The ``tests/avocado/avocado_qemu`` directory provides the
``avocado_qemu`` Python module, containing the ``avocado_qemu.Test``
class.  Here's a simple usage example:

.. code::

  from avocado_qemu import QemuSystemTest


  class Version(QemuSystemTest):
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
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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
and also an optional ``name`` attribute so you can identify a specific
machine and get it more than once through the tests methods. A simple
and hypothetical example follows:

.. code::

  from avocado_qemu import QemuSystemTest


  class MultipleMachines(QemuSystemTest):
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

The ``avocado_qemu.LinuxTest`` base test class
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``avocado_qemu.LinuxTest`` is further specialization of the
``avocado_qemu.Test`` class, so it contains all the characteristics of
the later plus some extra features.

First of all, this base class is intended for tests that need to
interact with a fully booted and operational Linux guest.  At this
time, it uses a Fedora 31 guest image.  The most basic example looks
like this:

.. code::

  from avocado_qemu import LinuxTest


  class SomeTest(LinuxTest):

      def test(self):
          self.launch_and_wait()
          self.ssh_command('some_command_to_be_run_in_the_guest')

Please refer to tests that use ``avocado_qemu.LinuxTest`` under
``tests/avocado`` for more examples.

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
^^^^^^^^^^^^^^^^^^^^^

The QEMU binary used for the ``self.vm`` QEMUMachine instance will
primarily depend on the value of the ``qemu_bin`` parameter.  If it's
not explicitly set, its default value will be the result of a dynamic
probe in the same source tree.  A suitable binary will be one that
targets the architecture matching host machine.

Based on this description, test writers will usually rely on one of
the following approaches:

1) Set ``qemu_bin``, and use the given binary

2) Do not set ``qemu_bin``, and use a QEMU binary named like
   "qemu-system-${arch}", either in the current
   working directory, or in the current source tree.

The resulting ``qemu_bin`` value will be preserved in the
``avocado_qemu.Test`` as an attribute with the same name.

Attribute reference
~~~~~~~~~~~~~~~~~~~

Test
^^^^

Besides the attributes and methods that are part of the base
``avocado.Test`` class, the following attributes are available on any
``avocado_qemu.Test`` instance.

vm
''

A QEMUMachine instance, initially configured according to the given
``qemu_bin`` parameter.

arch
''''

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

cpu
'''

The cpu model that will be set to all QEMUMachine instances created
by the test.

The ``cpu`` attribute will be set to the test parameter of the same
name. If one is not given explicitly, it will either be set to
``None ``, or, if the test is tagged with one (and only one)
``:avocado: tags=cpu:VALUE`` tag, it will be set to ``VALUE``.

machine
'''''''

The machine type that will be set to all QEMUMachine instances created
by the test.

The ``machine`` attribute will be set to the test parameter of the same
name.  If one is not given explicitly, it will either be set to
``None``, or, if the test is tagged with one (and only one)
``:avocado: tags=machine:VALUE`` tag, it will be set to ``VALUE``.

qemu_bin
''''''''

The preserved value of the ``qemu_bin`` parameter or the result of the
dynamic probe for a QEMU binary in the current working directory or
source tree.

LinuxTest
^^^^^^^^^

Besides the attributes present on the ``avocado_qemu.Test`` base
class, the ``avocado_qemu.LinuxTest`` adds the following attributes:

distro
''''''

The name of the Linux distribution used as the guest image for the
test.  The name should match the **Provider** column on the list
of images supported by the avocado.utils.vmimage library:

https://avocado-framework.readthedocs.io/en/latest/guides/writer/libs/vmimage.html#supported-images

distro_version
''''''''''''''

The version of the Linux distribution as the guest image for the
test.  The name should match the **Version** column on the list
of images supported by the avocado.utils.vmimage library:

https://avocado-framework.readthedocs.io/en/latest/guides/writer/libs/vmimage.html#supported-images

distro_checksum
'''''''''''''''

The sha256 hash of the guest image file used for the test.

If this value is not set in the code or by a test parameter (with the
same name), no validation on the integrity of the image will be
performed.

Parameter reference
~~~~~~~~~~~~~~~~~~~

To understand how Avocado parameters are accessed by tests, and how
they can be passed to tests, please refer to::

  https://avocado-framework.readthedocs.io/en/latest/guides/writer/chapters/writing.html#accessing-test-parameters

Parameter values can be easily seen in the log files, and will look
like the following:

.. code::

  PARAMS (key=qemu_bin, path=*, default=./qemu-system-x86_64) => './qemu-system-x86_64

Test
^^^^

arch
''''

The architecture that will influence the selection of a QEMU binary
(when one is not explicitly given).

Tests are also free to use this parameter value, for their own needs.
A test may, for instance, use the same value when selecting the
architecture of a kernel or disk image to boot a VM with.

This parameter has a direct relation with the ``arch`` attribute.  If
not given, it will default to None.

cpu
'''

The cpu model that will be set to all QEMUMachine instances created
by the test.

machine
'''''''

The machine type that will be set to all QEMUMachine instances created
by the test.

qemu_bin
''''''''

The exact QEMU binary to be used on QEMUMachine.

LinuxTest
^^^^^^^^^

Besides the parameters present on the ``avocado_qemu.Test`` base
class, the ``avocado_qemu.LinuxTest`` adds the following parameters:

distro
''''''

The name of the Linux distribution used as the guest image for the
test.  The name should match the **Provider** column on the list
of images supported by the avocado.utils.vmimage library:

https://avocado-framework.readthedocs.io/en/latest/guides/writer/libs/vmimage.html#supported-images

distro_version
''''''''''''''

The version of the Linux distribution as the guest image for the
test.  The name should match the **Version** column on the list
of images supported by the avocado.utils.vmimage library:

https://avocado-framework.readthedocs.io/en/latest/guides/writer/libs/vmimage.html#supported-images

distro_checksum
'''''''''''''''

The sha256 hash of the guest image file used for the test.

If this value is not set in the code or by this parameter no
validation on the integrity of the image will be performed.

Skipping tests
~~~~~~~~~~~~~~

The Avocado framework provides Python decorators which allow for easily skip
tests running under certain conditions. For example, on the lack of a binary
on the test system or when the running environment is a CI system. For further
information about those decorators, please refer to::

  https://avocado-framework.readthedocs.io/en/latest/guides/writer/chapters/writing.html#skipping-tests

While the conditions for skipping tests are often specifics of each one, there
are recurring scenarios identified by the QEMU developers and the use of
environment variables became a kind of standard way to enable/disable tests.

Here is a list of the most used variables:

AVOCADO_ALLOW_LARGE_STORAGE
^^^^^^^^^^^^^^^^^^^^^^^^^^^
Tests which are going to fetch or produce assets considered *large* are not
going to run unless that ``AVOCADO_ALLOW_LARGE_STORAGE=1`` is exported on
the environment.

The definition of *large* is a bit arbitrary here, but it usually means an
asset which occupies at least 1GB of size on disk when uncompressed.

AVOCADO_ALLOW_UNTRUSTED_CODE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
There are tests which will boot a kernel image or firmware that can be
considered not safe to run on the developer's workstation, thus they are
skipped by default. The definition of *not safe* is also arbitrary but
usually it means a blob which either its source or build process aren't
public available.

You should export ``AVOCADO_ALLOW_UNTRUSTED_CODE=1`` on the environment in
order to allow tests which make use of those kind of assets.

AVOCADO_TIMEOUT_EXPECTED
^^^^^^^^^^^^^^^^^^^^^^^^
The Avocado framework has a timeout mechanism which interrupts tests to avoid the
test suite of getting stuck. The timeout value can be set via test parameter or
property defined in the test class, for further details::

  https://avocado-framework.readthedocs.io/en/latest/guides/writer/chapters/writing.html#setting-a-test-timeout

Even though the timeout can be set by the test developer, there are some tests
that may not have a well-defined limit of time to finish under certain
conditions. For example, tests that take longer to execute when QEMU is
compiled with debug flags. Therefore, the ``AVOCADO_TIMEOUT_EXPECTED`` variable
has been used to determine whether those tests should run or not.

GITLAB_CI
^^^^^^^^^
A number of tests are flagged to not run on the GitLab CI. Usually because
they proved to the flaky or there are constraints on the CI environment which
would make them fail. If you encounter a similar situation then use that
variable as shown on the code snippet below to skip the test:

.. code::

  @skipIf(os.getenv('GITLAB_CI'), 'Running on GitLab')
  def test(self):
      do_something()

Uninstalling Avocado
~~~~~~~~~~~~~~~~~~~~

If you've followed the manual installation instructions above, you can
easily uninstall Avocado.  Start by listing the packages you have
installed::

  pip list --user

And remove any package you want with::

  pip uninstall <package_name>

If you've used ``make check-avocado``, the Python virtual environment where
Avocado is installed will be cleaned up as part of ``make check-clean``.

.. _checktcg-ref:

Testing with "make check-tcg"
-----------------------------

The check-tcg tests are intended for simple smoke tests of both
linux-user and softmmu TCG functionality. However to build test
programs for guest targets you need to have cross compilers available.
If your distribution supports cross compilers you can do something as
simple as::

  apt install gcc-aarch64-linux-gnu

The configure script will automatically pick up their presence.
Sometimes compilers have slightly odd names so the availability of
them can be prompted by passing in the appropriate configure option
for the architecture in question, for example::

  $(configure) --cross-cc-aarch64=aarch64-cc

There is also a ``--cross-cc-cflags-ARCH`` flag in case additional
compiler flags are needed to build for a given target.

If you have the ability to run containers as the user the build system
will automatically use them where no system compiler is available. For
architectures where we also support building QEMU we will generally
use the same container to build tests. However there are a number of
additional containers defined that have a minimal cross-build
environment that is only suitable for building test cases. Sometimes
we may use a bleeding edge distribution for compiler features needed
for test cases that aren't yet in the LTS distros we support for QEMU
itself.

See :ref:`container-ref` for more details.

Running subset of tests
~~~~~~~~~~~~~~~~~~~~~~~

You can build the tests for one architecture::

  make build-tcg-tests-$TARGET

And run with::

  make run-tcg-tests-$TARGET

Adding ``V=1`` to the invocation will show the details of how to
invoke QEMU for the test which is useful for debugging tests.

TCG test dependencies
~~~~~~~~~~~~~~~~~~~~~

The TCG tests are deliberately very light on dependencies and are
either totally bare with minimal gcc lib support (for softmmu tests)
or just glibc (for linux-user tests). This is because getting a cross
compiler to work with additional libraries can be challenging.

Other TCG Tests
---------------

There are a number of out-of-tree test suites that are used for more
extensive testing of processor features.

KVM Unit Tests
~~~~~~~~~~~~~~

The KVM unit tests are designed to run as a Guest OS under KVM but
there is no reason why they can't exercise the TCG as well. It
provides a minimal OS kernel with hooks for enabling the MMU as well
as reporting test results via a special device::

  https://git.kernel.org/pub/scm/virt/kvm/kvm-unit-tests.git

Linux Test Project
~~~~~~~~~~~~~~~~~~

The LTP is focused on exercising the syscall interface of a Linux
kernel. It checks that syscalls behave as documented and strives to
exercise as many corner cases as possible. It is a useful test suite
to run to exercise QEMU's linux-user code::

  https://linux-test-project.github.io/

GCC gcov support
----------------

``gcov`` is a GCC tool to analyze the testing coverage by
instrumenting the tested code. To use it, configure QEMU with
``--enable-gcov`` option and build. Then run the tests as usual.

If you want to gather coverage information on a single test the ``make
clean-gcda`` target can be used to delete any existing coverage
information before running a single test.

You can generate a HTML coverage report by executing ``make
coverage-html`` which will create
``meson-logs/coveragereport/index.html``.

Further analysis can be conducted by running the ``gcov`` command
directly on the various .gcda output files. Please read the ``gcov``
documentation for more information.
