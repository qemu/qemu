=============================================================================
ACPI/SMBIOS avocado tests using biosbits
=============================================================================

Biosbits is a software written by Josh Triplett that can be downloaded
from https://biosbits.org/. The github codebase can be found
`here <https://github.com/biosbits/bits/tree/master>`__. It is a software that executes
the bios components such as acpi and smbios tables directly through acpica
bios interpreter (a freely available C based library written by Intel,
downloadable from https://acpica.org/ and is included with biosbits) without an
operating system getting involved in between.
There are several advantages to directly testing the bios in a real physical
machine or VM as opposed to indirectly discovering bios issues through the
operating system. For one thing, the OSes tend to hide bios problems from the
end user. The other is that we have more control of what we wanted to test
and how by directly using acpica interpreter on top of the bios on a running
system. More details on the inspiration for developing biosbits and its real
life uses can be found in [#a]_ and [#b]_.
For QEMU, we maintain a fork of bios bits in gitlab along with all the
dependent submodules here: https://gitlab.com/qemu-project/biosbits-bits
This fork contains numerous fixes, a newer acpica and changes specific to
running this avocado QEMU tests using bits. The author of this document
is the sole maintainer of the QEMU fork of bios bits repo.

Under the directory ``tests/avocado/``, ``acpi-bits.py`` is a QEMU avocado
test that drives all this.

A brief description of the various test files follows.

Under ``tests/avocado/`` as the root we have:

::

   ├── acpi-bits
   │ ├── bits-config
   │ │ └── bits-cfg.txt
   │ ├── bits-tests
   │   ├── smbios.py2
   │   ├── testacpi.py2
   │   └── testcpuid.py2
   ├── acpi-bits.py

* ``tests/avocado``:

   ``acpi-bits.py``:
   This is the main python avocado test script that generates a
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
   build directory:
   ::

     $ make check-venv (needed only the first time to create the venv)
     $ ./tests/venv/bin/avocado run -t acpi tests/avocado

   The above will run all acpi avocado tests including this one.
   In order to run the individual tests, perform the following:
   ::

     $ ./tests/venv/bin/avocado run tests/avocado/acpi-bits.py --tap -

   The above will produce output in tap format. You can omit "--tap -" in the
   end and it will produce output like the following:
   ::

      $ ./tests/venv/bin/avocado run tests/avocado/acpi-bits.py
      Fetching asset from tests/avocado/acpi-bits.py:AcpiBitsTest.test_acpi_smbios_bits
      JOB ID     : eab225724da7b64c012c65705dc2fa14ab1defef
      JOB LOG    : /home/anisinha/avocado/job-results/job-2022-10-10T17.58-eab2257/job.log
      (1/1) tests/avocado/acpi-bits.py:AcpiBitsTest.test_acpi_smbios_bits: PASS (33.09 s)
      RESULTS    : PASS 1 | ERROR 0 | FAIL 0 | SKIP 0 | WARN 0 | INTERRUPT 0 | CANCEL 0
      JOB TIME   : 39.22 s

   You can inspect the log file for more information about the run or in order
   to diagnoze issues. If you pass V=1 in the environment, more diagnostic logs
   would be found in the test log.

* ``tests/avocado/acpi-bits/bits-config``:

   This location contains biosbits configuration files that determine how the
   software runs the tests.

   ``bits-config.txt``:
   This is the biosbits config file that determines what tests
   or actions are performed by bits. The description of the config options are
   provided in the file itself.

* ``tests/avocado/acpi-bits/bits-tests``:

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

   Commits (a), (b) and (c) should go under separate commits so that the original
   test script and the changes we have made are separated and clear.

   The test framework will then use your modified test script to run the test.
   No further changes would be needed. Please check the logs to make sure that
   appropriate changes have taken effect.

   The tests have an extension .py2 in order to indicate that:

   (a) They are python2.7 based scripts and not python 3 scripts.
   (b) They are run from within the bios bits VM and is not subjected to QEMU
       build/test python script maintenance and dependency resolutions.
   (c) They need not be loaded by avocado framework when running tests.


Author: Ani Sinha <ani@anisinha.ca>

References:
-----------
.. [#a] https://blog.linuxplumbersconf.org/2011/ocw/system/presentations/867/original/bits.pdf
.. [#b] https://www.youtube.com/watch?v=36QIepyUuhg

