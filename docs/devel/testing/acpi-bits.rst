==================================
ACPI/SMBIOS testing using biosbits
==================================
************
Introduction
************
Biosbits is a software written by Josh Triplett that can be downloaded
from https://biosbits.org/. The github codebase can be found
`here <https://github.com/biosbits/bits/tree/master>`__. It is a software that
executes the bios components such as acpi and smbios tables directly through
acpica bios interpreter (a freely available C based library written by Intel,
downloadable from https://acpica.org/ and is included with biosbits) without an
operating system getting involved in between. Bios-bits has python integration
with grub so actual routines that executes bios components can be written in
python instead of bash-ish (grub's native scripting language).
There are several advantages to directly testing the bios in a real physical
machine or in a VM as opposed to indirectly discovering bios issues through the
operating system (the OS). Operating systems tend to bypass bios problems and
hide them from the end user. We have more control of what we wanted to test and
how by being as close to the bios on a running system as possible without a
complicated software component such as an operating system coming in between.
Another issue is that we cannot exercise bios components such as ACPI and
SMBIOS without being in the highest hardware privilege level, ring 0 for
example in case of x86. Since the OS executes from ring 0 whereas normal user
land software resides in unprivileged ring 3, operating system must be modified
in order to write our test routines that exercise and test the bios. This is
not possible in all cases. Lastly, test frameworks and routines are preferably
written using a high level scripting language such as python. OSes and
OS modules are generally written using low level languages such as C and
low level assembly machine language. Writing test routines in a low level
language makes things more cumbersome. These and other reasons makes using
bios-bits very attractive for testing bioses. More details on the inspiration
for developing biosbits and its real life uses were presented `at Plumbers
in 2011 <Plumbers_>`__ and `at Linux.conf.au in 2012 <Linux.conf.au_>`__.

For QEMU, we maintain a fork of bios bits in `gitlab`_, along with all
the dependent submodules.  This fork contains numerous fixes, a newer
acpica and changes specific to running these functional QEMU tests using
bits. The author of this document is the current maintainer of the QEMU
fork of bios bits repository. For more information, please see `the
author's FOSDEM presentation <FOSDEM_>`__ on this bios-bits based test framework.

.. _Plumbers: https://blog.linuxplumbersconf.org/2011/ocw/system/presentations/867/original/bits.pdf
.. _Linux.conf.au: https://www.youtube.com/watch?v=36QIepyUuhg
.. _gitlab: https://gitlab.com/qemu-project/biosbits-bits
.. _FOSDEM: https://fosdem.org/2024/schedule/event/fosdem-2024-2262-exercising-qemu-generated-acpi-smbios-tables-using-biosbits-from-within-a-guest-vm-/

*********************************
Description of the test framework
*********************************

Under the directory ``tests/functional/``, ``test_acpi_bits.py`` is a QEMU
functional test that drives all this.

A brief description of the various test files follows.

Under ``tests/functional/`` as the root we have:

::

   ├── acpi-bits
   │ ├── bits-config
   │ │ └── bits-cfg.txt
   │ ├── bits-tests
   │   ├── smbios.py2
   │   ├── testacpi.py2
   │   └── testcpuid.py2
   ├── test_acpi_bits.py

* ``tests/functional``:

   ``test_acpi_bits.py``:
   This is the main python functional test script that generates a
   biosbits iso. It then spawns a QEMU VM with it, collects the log and reports
   test failures. This is the script one would be interested in if they wanted
   to add or change some component of the log parsing, add a new command line
   to alter how QEMU is spawned etc. Test writers typically would not need to
   modify this script unless they wanted to enhance or change the log parsing
   for their tests. In order to enable debugging, you can set **V=1**
   environment variable. This enables verbose mode for the test and also dumps
   the entire log from bios bits and more information in case failure happens.
   You can also set **BITS_DEBUG=1** to turn on debug mode. It will enable
   verbose logs and also retain the temporary work directory the test used for
   you to inspect and run the specific commands manually.

   In order to run this test, please perform the following steps from the QEMU
   build directory (assuming that the sources are in ".."):
   ::

     $ export PYTHONPATH=../python:../tests/functional
     $ export QEMU_TEST_QEMU_BINARY=$PWD/qemu-system-x86_64
     $ python3 ../tests/functional/test_acpi_bits.py

   The above will run all acpi-bits functional tests (producing output in
   tap format).

   You can inspect the log files in tests/functional/x86_64/test_acpi_bits.*/
   for more information about the run or in order to diagnoze issues.
   If you pass V=1 in the environment, more diagnostic logs will be put into
   the test log.

* ``tests/functional/acpi-bits/bits-config``:

   This location contains biosbits configuration files that determine how the
   software runs the tests.

   ``bits-config.txt``:
   This is the biosbits config file that determines what tests
   or actions are performed by bits. The description of the config options are
   provided in the file itself.

* ``tests/functional/acpi-bits/bits-tests``:

   This directory contains biosbits python based tests that are run from within
   the biosbits environment in the spawned VM. New additions of test cases can
   be made in the appropriate test file. For example, new acpi tests can go
   into testacpi.py2 and one would call testsuite.add_test() to register the new
   test so that it gets executed as a part of the ACPI tests.
   It might be occasionally necessary to disable some subtests or add a new
   test that belongs to a test suite not already present in this directory. To
   do this, please clone the bits source from
   https://gitlab.com/qemu-project/biosbits-bits/-/tree/qemu-bits.
   Note that this is the "qemu-bits" branch and not the "bits" branch of the
   repository. "qemu-bits" is the branch where we have made all the QEMU
   specific enhancements and we must use the source from this branch only.
   Copy the test suite/script that needs modification (addition of new tests
   or disabling them) from python directory into this directory. For
   example, in order to change cpuid related tests, copy the following
   file into this directory and rename it with .py2 extension:
   https://gitlab.com/qemu-project/biosbits-bits/-/blob/qemu-bits/python/testcpuid.py
   Then make your additions and changes here. Therefore, the steps are:

       (a) Copy unmodified test script to this directory from bits source.
       (b) Add a SPDX license header.
       (c) Perform modifications to the test.

   Commits (a), (b) and (c) preferably should go under separate commits so that
   the original test script and the changes we have made are separated and
   clear. (a) and (b) can sometimes be combined into a single step.

   The test framework will then use your modified test script to run the test.
   No further changes would be needed. Please check the logs to make sure that
   appropriate changes have taken effect.

   The tests have an extension .py2 in order to indicate that:

   (a) They are python2.7 based scripts and not python 3 scripts.
   (b) They are run from within the bios bits VM and is not subjected to QEMU
       build/test python script maintenance and dependency resolutions.
   (c) They need not be loaded by the test framework by accident when running
       tests.


Author: Ani Sinha <anisinha@redhat.com>

