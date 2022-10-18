==================================
The QEMU build system architecture
==================================

This document aims to help developers understand the architecture of the
QEMU build system. As with projects using GNU autotools, the QEMU build
system has two stages; first the developer runs the "configure" script
to determine the local build environment characteristics, then they run
"make" to build the project.  This is about where the similarities with
GNU autotools end, so try to forget what you know about them.

The two general ways to perform a build are as follows:

 - build artifacts outside of QEMU source tree entirely::

     cd ../
     mkdir build
     cd build
     ../qemu/configure
     make

 - build artifacts in a subdir of QEMU source tree::

     mkdir build
     cd build
     ../configure
     make

Most of the actual build process uses Meson under the hood, therefore
build artifacts cannot be placed in the source tree itself.


Stage 1: configure
==================

The configure script has five tasks:

 - detect the host architecture

 - list the targets for which to build emulators; the list of
   targets also affects which firmware binaries and tests to build

 - find the compilers (native and cross) used to build executables,
   firmware and tests.  The results are written as either Makefile
   fragments (``config-host.mak``) or a Meson machine file
   (``config-meson.cross``)

 - create a virtual environment in which all Python code runs during
   the build, and possibly install packages into it from PyPI

 - invoke Meson in the virtual environment, to perform the actual
   configuration step for the emulator build

The configure script automatically recognizes command line options for
which a same-named Meson option exists; dashes in the command line are
replaced with underscores.

Almost all QEMU developers that need to modify the build system will
only be concerned with Meson, and therefore can skip the rest of this
section.


Modifying ``configure``
-----------------------

``configure`` is a shell script; it uses ``#!/bin/sh`` and therefore
should be compatible with any POSIX shell. It is important to avoid
using bash-isms to avoid breaking development platforms where bash is
the primary host.

The configure script provides a variety of functions to help writing
portable shell code and providing consistent behavior across architectures
and operating systems:

``error_exit $MESSAGE $MORE...``
   Print $MESSAGE to stderr, followed by $MORE... and then exit from the
   configure script with non-zero status.

``has $COMMAND``
   Determine if $COMMAND exists in the current environment, either as a
   shell builtin, or executable binary, returning 0 on success.  The
   replacement in Meson is ``find_program()``.

``probe_target_compiler $TARGET``
  Detect a cross compiler and cross tools for the QEMU target $TARGET (e.g.,
  ``$CPU-softmmu``, ``$CPU-linux-user``, ``$CPU-bsd-user``).  If a working
  compiler is present, return success and set variables ``$target_cc``,
  ``$target_ar``, etc. to non-empty values.

``write_target_makefile``
  Write a Makefile fragment to stdout, exposing the result of the most
  ``probe_target_compiler`` call as the usual Make variables (``CC``,
  ``AR``, ``LD``, etc.).


Configure does not generally perform tests for compiler options beyond
basic checks to detect the host platform and ensure the compiler is
functioning.  These are performed using a few more helper functions:

``compile_object $CFLAGS``
   Attempt to compile a test program with the system C compiler using
   $CFLAGS. The test program must have been previously written to a file
   called $TMPC.

``compile_prog $CFLAGS $LDFLAGS``
   Attempt to compile a test program with the system C compiler using
   $CFLAGS and link it with the system linker using $LDFLAGS. The test
   program must have been previously written to a file called $TMPC.

``check_define $NAME``
   Determine if the macro $NAME is defined by the system C compiler.

``do_compiler $CC $ARGS...``
   Attempt to run the C compiler $CC, passing it $ARGS...  This function
   does not use flags passed via options such as ``--extra-cflags``, and
   therefore can be used to check for cross compilers.  However, most
   such checks are done at ``make`` time instead (see for example the
   ``cc-option`` macro in ``pc-bios/option-rom/Makefile``).

``write_c_skeleton``
   Write a minimal C program main() function to the temporary file
   indicated by $TMPC.


Python virtual environments and the QEMU build system
-----------------------------------------------------

TBD

Stage 2: Meson
==============

The Meson build system describes the build and install process for:

1) executables, which include:

   - Tools - ``qemu-img``, ``qemu-nbd``, ``qemu-ga`` (guest agent), etc

   - System emulators - ``qemu-system-$ARCH``

   - Userspace emulators - ``qemu-$ARCH``

   - Unit tests

2) documentation

3) ROMs, whether provided as binary blobs in the QEMU distributions
   or cross compiled under the direction of the configure script

4) other data files, such as icons or desktop files

All executables are built by default, except for some ``contrib/``
binaries that are known to fail to build on some platforms (for example
32-bit or big-endian platforms).  Tests are also built by default,
though that might change in the future.

The source code is highly modularized, split across many files to
facilitate building of all of these components with as little duplicated
compilation as possible. Using the Meson "sourceset" functionality,
``meson.build`` files group the source files in rules that are
enabled according to the available system libraries and to various
configuration symbols.  Sourcesets belong to one of four groups:

Subsystem sourcesets:
  Various subsystems that are common to both tools and emulators have
  their own sourceset, for example ``block_ss`` for the block device subsystem,
  ``chardev_ss`` for the character device subsystem, etc.  These sourcesets
  are then turned into static libraries as follows::

    libchardev = static_library('chardev', chardev_ss.sources(),
                                name_suffix: 'fa',
                                build_by_default: false)

    chardev = declare_dependency(link_whole: libchardev)

  As of Meson 0.55.1, the special ``.fa`` suffix should be used for everything
  that is used with ``link_whole``, to ensure that the link flags are placed
  correctly in the command line.

Target-independent emulator sourcesets:
  Various general purpose helper code is compiled only once and
  the .o files are linked into all output binaries that need it.
  This includes error handling infrastructure, standard data structures,
  platform portability wrapper functions, etc.

  Target-independent code lives in the ``common_ss``, ``softmmu_ss`` and
  ``user_ss`` sourcesets.  ``common_ss`` is linked into all emulators,
  ``softmmu_ss`` only in system emulators, ``user_ss`` only in user-mode
  emulators.

  Target-independent sourcesets must exercise particular care when using
  ``if_false`` rules.  The ``if_false`` rule will be used correctly when linking
  emulator binaries; however, when *compiling* target-independent files
  into .o files, Meson may need to pick *both* the ``if_true`` and
  ``if_false`` sides to cater for targets that want either side.  To
  achieve that, you can add a special rule using the ``CONFIG_ALL``
  symbol::

    # Some targets have CONFIG_ACPI, some don't, so this is not enough
    softmmu_ss.add(when: 'CONFIG_ACPI', if_true: files('acpi.c'),
                                        if_false: files('acpi-stub.c'))

    # This is required as well:
    softmmu_ss.add(when: 'CONFIG_ALL', if_true: files('acpi-stub.c'))

Target-dependent emulator sourcesets:
  In the target-dependent set lives CPU emulation, some device emulation and
  much glue code. This sometimes also has to be compiled multiple times,
  once for each target being built.  Target-dependent files are included
  in the ``specific_ss`` sourceset.

  Each emulator also includes sources for files in the ``hw/`` and ``target/``
  subdirectories.  The subdirectory used for each emulator comes
  from the target's definition of ``TARGET_BASE_ARCH`` or (if missing)
  ``TARGET_ARCH``, as found in ``default-configs/targets/*.mak``.

  Each subdirectory in ``hw/`` adds one sourceset to the ``hw_arch`` dictionary,
  for example::

    arm_ss = ss.source_set()
    arm_ss.add(files('boot.c'), fdt)
    ...
    hw_arch += {'arm': arm_ss}

  The sourceset is only used for system emulators.

  Each subdirectory in ``target/`` instead should add one sourceset to each
  of the ``target_arch`` and ``target_softmmu_arch``, which are used respectively
  for all emulators and for system emulators only.  For example::

    arm_ss = ss.source_set()
    arm_softmmu_ss = ss.source_set()
    ...
    target_arch += {'arm': arm_ss}
    target_softmmu_arch += {'arm': arm_softmmu_ss}

Module sourcesets:
  There are two dictionaries for modules: ``modules`` is used for
  target-independent modules and ``target_modules`` is used for
  target-dependent modules.  When modules are disabled the ``module``
  source sets are added to ``softmmu_ss`` and the ``target_modules``
  source sets are added to ``specific_ss``.

  Both dictionaries are nested.  One dictionary is created per
  subdirectory, and these per-subdirectory dictionaries are added to
  the toplevel dictionaries.  For example::

    hw_display_modules = {}
    qxl_ss = ss.source_set()
    ...
    hw_display_modules += { 'qxl': qxl_ss }
    modules += { 'hw-display': hw_display_modules }

Utility sourcesets:
  All binaries link with a static library ``libqemuutil.a``.  This library
  is built from several sourcesets; most of them however host generated
  code, and the only two of general interest are ``util_ss`` and ``stub_ss``.

  The separation between these two is purely for documentation purposes.
  ``util_ss`` contains generic utility files.  Even though this code is only
  linked in some binaries, sometimes it requires hooks only in some of
  these and depend on other functions that are not fully implemented by
  all QEMU binaries.  ``stub_ss`` links dummy stubs that will only be linked
  into the binary if the real implementation is not present.  In a way,
  the stubs can be thought of as a portable implementation of the weak
  symbols concept.


The following files concur in the definition of which files are linked
into each emulator:

``default-configs/devices/*.mak``
  The files under ``default-configs/devices/`` control the boards and devices
  that are built into each QEMU system emulation targets. They merely contain
  a list of config variable definitions such as::

    include arm-softmmu.mak
    CONFIG_XLNX_ZYNQMP_ARM=y
    CONFIG_XLNX_VERSAL=y

``*/Kconfig``
  These files are processed together with ``default-configs/devices/*.mak`` and
  describe the dependencies between various features, subsystems and
  device models.  They are described in :ref:`kconfig`

``default-configs/targets/*.mak``
  These files mostly define symbols that appear in the ``*-config-target.h``
  file for each emulator [#cfgtarget]_.  However, the ``TARGET_ARCH``
  and ``TARGET_BASE_ARCH`` will also be used to select the ``hw/`` and
  ``target/`` subdirectories that are compiled into each target.

.. [#cfgtarget] This header is included by ``qemu/osdep.h`` when
                compiling files from the target-specific sourcesets.

These files rarely need changing unless you are adding a completely
new target, or enabling new devices or hardware for a particular
system/userspace emulation target


Adding checks
-------------

Compiler checks can be as simple as the following::

  config_host_data.set('HAVE_BTRFS_H', cc.has_header('linux/btrfs.h'))

A more complex task such as adding a new dependency usually
comprises the following tasks:

 - Add a Meson build option to meson_options.txt.

 - Add code to perform the actual feature check.

 - Add code to include the feature status in ``config-host.h``

 - Add code to print out the feature status in the configure summary
   upon completion.

Taking the probe for SDL2_Image as an example, we have the following
in ``meson_options.txt``::

  option('sdl_image', type : 'feature', value : 'auto',
         description: 'SDL Image support for icons')

Unless the option was given a non-``auto`` value (on the configure
command line), the detection code must be performed only if the
dependency will be used::

  sdl_image = not_found
  if not get_option('sdl_image').auto() or have_system
    sdl_image = dependency('SDL2_image', required: get_option('sdl_image'),
                           method: 'pkg-config')
  endif

This avoids warnings on static builds of user-mode emulators, for example.
Most of the libraries used by system-mode emulators are not available for
static linking.

The other supporting code is generally simple::

  # Create config-host.h (if applicable)
  config_host_data.set('CONFIG_SDL_IMAGE', sdl_image.found())

  # Summary
  summary_info += {'SDL image support': sdl_image.found()}

For the configure script to parse the new option, the
``scripts/meson-buildoptions.sh`` file must be up-to-date; ``make
update-buildoptions`` (or just ``make``) will take care of updating it.


Support scripts
---------------

Meson has a special convention for invoking Python scripts: if their
first line is ``#! /usr/bin/env python3`` and the file is *not* executable,
find_program() arranges to invoke the script under the same Python
interpreter that was used to invoke Meson.  This is the most common
and preferred way to invoke support scripts from Meson build files,
because it automatically uses the value of configure's --python= option.

In case the script is not written in Python, use a ``#! /usr/bin/env ...``
line and make the script executable.

Scripts written in Python, where it is desirable to make the script
executable (for example for test scripts that developers may want to
invoke from the command line, such as tests/qapi-schema/test-qapi.py),
should be invoked through the ``python`` variable in meson.build. For
example::

  test('QAPI schema regression tests', python,
       args: files('test-qapi.py'),
       env: test_env, suite: ['qapi-schema', 'qapi-frontend'])

This is needed to obey the --python= option passed to the configure
script, which may point to something other than the first python3
binary on the path.


Stage 3: Make
=============

The next step in building QEMU is to invoke make.  GNU Make is required
to build QEMU, and may be installed as ``gmake`` on some hosts.

The output of Meson is a ``build.ninja`` file, which is used with the
Ninja build tool.  However, QEMU's build comprises other components than
just the emulators (namely firmware and the tests in ``tests/tcg``) which
need different cross compilers.  The QEMU Makefile wraps both Ninja and
the smaller build systems for firmware and tests; it also takes care of
running ``configure`` again when the script changes.  Apart from invoking
these sub-Makefiles, the resulting build is largely non-recursive.

Tests, whether defined in ``meson.build`` or not, are also ran by the
Makefile with the traditional ``make check`` phony target, while benchmarks
are run with ``make bench``.  Meson test suites such as ``unit`` can be ran
with ``make check-unit``, and ``make check-tcg`` builds and runs "non-Meson"
tests for all targets.

If desired, it is also possible to use ``ninja`` and ``meson test``,
respectively to build emulators and run tests defined in meson.build.
The main difference is that ``make`` needs the ``-jN`` flag in order to
enable parallel builds or tests.

Useful make targets
-------------------

``help``
  Print a help message for the most common build targets.

``print-VAR``
  Print the value of the variable VAR. Useful for debugging the build
  system.


Important files for the build system
====================================

Statically defined files
------------------------

The following key files are statically defined in the source tree, with
the rules needed to build QEMU. Their behaviour is influenced by a
number of dynamically created files listed later.

``Makefile``
  The main entry point used when invoking make to build all the components
  of QEMU. The default 'all' target will naturally result in the build of
  every component.

``*/meson.build``
  The meson.build file in the root directory is the main entry point for the
  Meson build system, and it coordinates the configuration and build of all
  executables.  Build rules for various subdirectories are included in
  other meson.build files spread throughout the QEMU source tree.

``tests/Makefile.include``
  Rules for external test harnesses. These include the TCG tests
  and the Avocado-based integration tests.

``tests/docker/Makefile.include``
  Rules for Docker tests. Like ``tests/Makefile.include``, this file is
  included directly by the top level Makefile, anything defined in this
  file will influence the entire build system.

``tests/vm/Makefile.include``
  Rules for VM-based tests. Like ``tests/Makefile.include``, this file is
  included directly by the top level Makefile, anything defined in this
  file will influence the entire build system.

Dynamically created files
-------------------------

The following files are generated at run-time in order to control the
behaviour of the Makefiles. This avoids the need for QEMU makefiles to
go through any pre-processing as seen with autotools, where configure
generates ``Makefile`` from ``Makefile.in``.

Built by configure:

``config-host.mak``
  When configure has determined the characteristics of the build host it
  will write them to this file for use in ``Makefile`` and to a smaller
  extent ``meson.build``. These include the paths to various tools and a
  variety of ``CONFIG_*`` variables related to optionally enabled features.

  ``config-host.mak`` is also used as a dependency checking mechanism. If make
  sees that the modification timestamp on configure is newer than that on
  ``config-host.mak``, then configure will be re-run.

  The variables defined here apply to all QEMU
  build outputs.

``config-meson.cross``

  A Meson "cross file" (or native file) used to communicate the paths to
  the toolchain and other configuration options.

``config.status``

  A small shell script that will invoke configure again with the same
  environment variables that were set during the first run.  It's used to
  rerun configure after changes to the source code, but it can also be
  inspected manually to check the contents of the environment.

``Makefile.prereqs``

  A set of Makefile dependencies that order the build and execution of
  firmware and tests after the container images and emulators that they
  need.

``pc-bios/*/config.mak``, ``tests/tcg/config-host.mak``, ``tests/tcg/*/config-target.mak``

  Configuration variables used to build the firmware and TCG tests,
  including paths to cross compilation toolchains.

``pyvenv``

  A Python virtual environment that is used for all Python code running
  during the build.  Using a virtual environment ensures that even code
  that is run via ``sphinx-build``, ``meson`` etc. uses the same interpreter
  and packages.

Built by Meson:

``config-host.h``
  Used by C code to determine the properties of the build environment
  and the set of enabled features for the entire build.

``${TARGET-NAME}-config-devices.mak``
  TARGET-NAME is the name of a system emulator. The file is
  generated by Meson using files under ``configs/devices`` as input.

``${TARGET-NAME}-config-target.mak``
  TARGET-NAME is the name of a system or usermode emulator. The file is
  generated by Meson using files under ``configs/targets`` as input.

``$TARGET_NAME-config-target.h``, ``$TARGET_NAME-config-devices.h``
  Used by C code to determine the properties and enabled
  features for each target.  enabled.  They are generated from
  the contents of the corresponding ``*.mak`` files using Meson's
  ``configure_file()`` function; each target can include them using
  the ``CONFIG_TARGET`` and ``CONFIG_DEVICES`` macro respectively.

``build.ninja``
  The build rules.


Built by Makefile:

``Makefile.ninja``
  A Makefile include that bridges to ninja for the actual build.  The
  Makefile is mostly a list of targets that Meson included in build.ninja.

``Makefile.mtest``
  The Makefile definitions that let "make check" run tests defined in
  meson.build.  The rules are produced from Meson's JSON description of
  tests (obtained with "meson introspect --tests") through the script
  scripts/mtest2make.py.
