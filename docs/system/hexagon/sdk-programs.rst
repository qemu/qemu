.. SPDX-License-Identifier: GPL-2.0-or-later

Running SDK programs
====================

``qemu-system-hexagon`` can run SDK programs that normally run under
``hexagon-sim``. The boot image depends on the runtime the program expects:

.. list-table::
   :header-rows: 1

   * - Use case
     - Initial image
     - Subsequent stages
   * - SDK standalone OS
     - ``-kernel prog.elf``
     - Startup code calls the program's ``main()``
   * - SDK QuRT dynamic program
     - ``-kernel runelf.pbn``
     - QuRT loads ``run_main_on_hexagon_sim``, which loads ``userprog.so``

SDK programs and semihosting
----------------------------

The simulator-compatible machines, such as ``V66G_1024`` and
``V68N_1024``, provide semihosting by default. The SDK runtime uses
``trap0`` calls for services such as console I/O, host file access, and
program termination.

See :ref:`hexagon-semihosting` for more info.

The examples below use v68 SDK artifacts with ``-M V68N_1024``.

Standalone OS programs
~~~~~~~~~~~~~~~~~~~~~~

By default, ``hexagon-clang`` links a bare-metal executable with the SDK's
standalone OS startup code and C runtime. This small runtime initializes
the machine, calls ``main()``, and implements hosted services through
semihosting.

For example, save the following as ``prog.c``:

.. code-block:: c

    #include <stdio.h>

    int main(int argc, char **argv)
    {
        for (int i = 0; i < argc; i++) {
            printf("argv[%d] = %s\n", i, argv[i]);
        }
        return 0;
    }

With the SDK tools on ``PATH``, build and run it with:

.. code-block:: sh

    hexagon-clang -mv68 prog.c -o prog.elf
    qemu-system-hexagon -M V68N_1024 -display none \
        -kernel ./prog.elf -append "foo bar"

The program prints:

.. code-block:: text

    argv[0] = ./prog.elf
    argv[1] = foo
    argv[2] = bar

Guest command-line arguments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example above passes the arguments with ``-append``. QEMU uses the
``-kernel`` filename as the first semihosting argument and splits
``-append`` on spaces to supply the remaining arguments. The host shell
quotes keep ``foo bar`` together as the value of the QEMU option; the
guest runtime subsequently parses the semihosting command line.

You can instead specify the semihosting command line with ``arg=`` options:

.. code-block:: sh

    qemu-system-hexagon -M V68N_1024 -display none -kernel ./prog.elf \
        -semihosting-config 'enable=on,target=native,arg=./prog.elf,arg=foo,arg=bar'

Include the program name as the first ``arg=`` value.

``target=native`` selects QEMU's implementation of semihosting even when
a debugger is connected. To connect the semihosting console explicitly to
the terminal, add a character device and select it with ``chardev=``:

.. code-block:: sh

    qemu-system-hexagon -M V68N_1024 -display none \
        -chardev stdio,id=semi \
        -semihosting-config enable=on,target=native,chardev=semi \
        -kernel ./prog.elf -append "foo bar"

QuRT programs and the SDK loader chain
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An SDK QuRT dynamic test consists of three guest images:

1. ``runelf.pbn`` boots QuRT and loads a user process from a host file.
2. ``run_main_on_hexagon_sim`` is that process. It initializes the dynamic
   loader and loads the application's shared object.
3. ``userprog.so`` exports ``main(int argc, char **argv)``, which the
   runner invokes.

QEMU initially loads only ``runelf.pbn``. The subsequent stages read the
other images through semihosting. Use matching SDK architecture variants
for the boot image, runner, and application. For example, with Hexagon
SDK 6.4 and its toolv19 v68 artifacts:

.. code-block:: sh

    runelf="$HEXAGON_SDK_ROOT/rtos/qurt/computev68/sdksim_bin/runelf.pbn"
    runner="$HEXAGON_SDK_ROOT/libs/run_main_on_hexagon/ship/hexagon_toolv19_v68/run_main_on_hexagon_sim"
    qemu-system-hexagon -M V68N_1024 -display none \
        -kernel "$runelf" -append "$runner -- ./userprog.so foo bar"

Build ``userprog.so`` with the SDK's QuRT shared-library build rules,
including any libraries needed by the application, and place its shared
dependencies where the SDK dynamic loader can find them.

The command line exposed through semihosting is:

.. code-block:: text

    <runelf.pbn path> <runner path> -- ./userprog.so foo bar

The fields have the following roles:

.. csv-table::
   :header: "Field", "Consumer", "Meaning"

   <runelf.pbn path>, SDK startup, Boot image name
   <runner path>, runelf.pbn, Process ELF to load
   --, run_main_on_hexagon_sim, End of runner options
   ./userprog.so, Dynamic loader, Shared object exporting main()
   foo bar, Application, argv[1] and argv[2]

The application's ``argv[0]`` is ``./userprog.so``. When translating an
SDK simulator command ``runelf.pbn -- runner -- userprog.so``, omit the
separator immediately after ``runelf.pbn`` from QEMU's ``-append`` value.
The separator after the runner remains part of the guest command line.

The equivalent explicit semihosting configuration is:

.. code-block:: sh

    qemu-system-hexagon -M V68N_1024 -display none -kernel "$runelf" \
        -semihosting-config "enable=on,target=native,arg=$runelf,arg=$runner,arg=--,arg=./userprog.so,arg=foo,arg=bar"
