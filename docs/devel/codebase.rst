========
Codebase
========

This section presents the various parts of QEMU and how the codebase is
organized.

Beyond giving succinct descriptions, the goal is to offer links to various
parts of the documentation/codebase.

Subsystems
----------

An exhaustive list of subsystems and associated files can be found in the
`MAINTAINERS <https://gitlab.com/qemu-project/qemu/-/blob/master/MAINTAINERS>`_
file.

Some of the main QEMU subsystems are:

- `Accelerators<Accelerators>`
- Block devices and `disk images<disk images>` support
- `CI<ci>` and `Tests<testing>`
- `Devices<device-emulation>` & Board models
- `Documentation <documentation-root>`
- `GDB support<GDB usage>`
- :ref:`Migration<migration>`
- `Monitor<QEMU monitor>`
- :ref:`QOM (QEMU Object Model)<qom>`
- `System mode<System emulation>`
- :ref:`TCG (Tiny Code Generator)<tcg>`
- `User mode<user-mode>` (`Linux<linux-user-mode>` & `BSD<bsd-user-mode>`)
- User Interfaces

More documentation on QEMU subsystems can be found on :ref:`internal-subsystem`
page.

The Grand tour
--------------

We present briefly here what every folder in the top directory of the codebase
contains. Hop on!

The folder name links here will take you to that folder in our gitlab
repository. Other links will take you to more detailed documentation for that
subsystem, where we have it. Unfortunately not every subsystem has documentation
yet, so sometimes the source code is all you have.

* `accel <https://gitlab.com/qemu-project/qemu/-/tree/master/accel>`_:
  Infrastructure and architecture agnostic code related to the various
  `accelerators <Accelerators>` supported by QEMU
  (TCG, KVM, hvf, whpx, xen, nvmm).
  Contains interfaces for operations that will be implemented per
  `target <https://gitlab.com/qemu-project/qemu/-/tree/master/target>`_.
* `audio <https://gitlab.com/qemu-project/qemu/-/tree/master/audio>`_:
  Audio (host) support.
* `authz <https://gitlab.com/qemu-project/qemu/-/tree/master/authz>`_:
  `QEMU Authorization framework<client authorization>`.
* `backends <https://gitlab.com/qemu-project/qemu/-/tree/master/backends>`_:
  Various backends that are used to access resources on the host (e.g. for
  random number generation, memory backing or cryptographic functions).
* `block <https://gitlab.com/qemu-project/qemu/-/tree/master/block>`_:
  Block devices and `image formats<disk images>` implementation.
* `bsd-user <https://gitlab.com/qemu-project/qemu/-/tree/master/bsd-user>`_:
  `BSD User mode<bsd-user-mode>`.
* build: Where the code built goes by default. You can tell the QEMU build
  system to put the built code anywhere else you like.
* `chardev <https://gitlab.com/qemu-project/qemu/-/tree/master/chardev>`_:
  Various backends used by char devices.
* `common-user <https://gitlab.com/qemu-project/qemu/-/tree/master/common-user>`_:
  User-mode assembly code for dealing with signals occurring during syscalls.
* `configs <https://gitlab.com/qemu-project/qemu/-/tree/master/configs>`_:
  Makefiles defining configurations to build QEMU.
* `contrib <https://gitlab.com/qemu-project/qemu/-/tree/master/contrib>`_:
  Community contributed devices/plugins/tools.
* `crypto <https://gitlab.com/qemu-project/qemu/-/tree/master/crypto>`_:
  Cryptographic algorithms used in QEMU.
* `disas <https://gitlab.com/qemu-project/qemu/-/tree/master/disas>`_:
  Disassembly functions used by QEMU target code.
* `docs <https://gitlab.com/qemu-project/qemu/-/tree/master/docs>`_:
  QEMU Documentation.
* `dump <https://gitlab.com/qemu-project/qemu/-/tree/master/dump>`_:
  Code to dump memory of a running VM.
* `ebpf <https://gitlab.com/qemu-project/qemu/-/tree/master/ebpf>`_:
  eBPF program support in QEMU. `virtio-net RSS<ebpf-rss>` uses it.
* `fpu <https://gitlab.com/qemu-project/qemu/-/tree/master/fpu>`_:
  Floating-point software emulation.
* `fsdev <https://gitlab.com/qemu-project/qemu/-/tree/master/fsdev>`_:
  `VirtFS <https://www.linux-kvm.org/page/VirtFS>`_ support.
* `gdbstub <https://gitlab.com/qemu-project/qemu/-/tree/master/gdbstub>`_:
  `GDB <GDB usage>` support.
* `gdb-xml <https://gitlab.com/qemu-project/qemu/-/tree/master/gdb-xml>`_:
  Set of XML files describing architectures and used by `gdbstub <GDB usage>`.
* `host <https://gitlab.com/qemu-project/qemu/-/tree/master/host>`_:
  Various architecture specific header files (crypto, atomic, memory
  operations).
* `linux-headers <https://gitlab.com/qemu-project/qemu/-/tree/master/linux-headers>`_:
  A subset of headers imported from Linux kernel and used for implementing
  KVM support and user-mode.
* `linux-user <https://gitlab.com/qemu-project/qemu/-/tree/master/linux-user>`_:
  `User mode <user-mode>` implementation. Contains one folder per target
  architecture.
* `.gitlab-ci.d <https://gitlab.com/qemu-project/qemu/-/tree/master/.gitlab-ci.d>`_:
  `CI <ci>` yaml and scripts.
* `include <https://gitlab.com/qemu-project/qemu/-/tree/master/include>`_:
  All headers associated to different subsystems in QEMU. The hierarchy used
  mirrors source code organization and naming.
* `hw <https://gitlab.com/qemu-project/qemu/-/tree/master/hw>`_:
  `Devices <device-emulation>` and boards emulation. Devices are categorized by
  type/protocol/architecture and located in associated subfolder.
* `io <https://gitlab.com/qemu-project/qemu/-/tree/master/io>`_:
  QEMU `I/O channels <https://lists.gnu.org/archive/html/qemu-devel/2015-11/msg04208.html>`_.
* `libdecnumber <https://gitlab.com/qemu-project/qemu/-/tree/master/libdecnumber>`_:
  Import of gcc library, used to implement decimal number arithmetic.
* `migration <https://gitlab.com/qemu-project/qemu/-/tree/master/migration>`__:
  :ref:`Migration framework <migration>`.
* `monitor <https://gitlab.com/qemu-project/qemu/-/tree/master/monitor>`_:
  `Monitor <QEMU monitor>` implementation (HMP & QMP).
* `nbd <https://gitlab.com/qemu-project/qemu/-/tree/master/nbd>`_:
  QEMU `NBD (Network Block Device) <nbd>` server.
* `net <https://gitlab.com/qemu-project/qemu/-/tree/master/net>`_:
  Network (host) support.
* `pc-bios <https://gitlab.com/qemu-project/qemu/-/tree/master/pc-bios>`_:
  Contains pre-built firmware binaries and boot images, ready to use in
  QEMU without compilation.
* `plugins <https://gitlab.com/qemu-project/qemu/-/tree/master/plugins>`_:
  :ref:`TCG plugins <tcg-plugins>` core implementation. Plugins can be found in
  `tests <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/tcg/plugins>`__
  and `contrib <https://gitlab.com/qemu-project/qemu/-/tree/master/contrib/plugins>`__
  folders.
* `po <https://gitlab.com/qemu-project/qemu/-/tree/master/po>`_:
  Translation files.
* `python <https://gitlab.com/qemu-project/qemu/-/tree/master/python>`_:
  Python part of our build/test system.
* `qapi <https://gitlab.com/qemu-project/qemu/-/tree/master/qapi>`_:
  `QAPI <qapi>` implementation.
* `qobject <https://gitlab.com/qemu-project/qemu/-/tree/master/qobject>`_:
  QEMU Object implementation.
* `qga <https://gitlab.com/qemu-project/qemu/-/tree/master/qga>`_:
  QEMU `Guest agent <qemu-ga>` implementation.
* `qom <https://gitlab.com/qemu-project/qemu/-/tree/master/qom>`_:
  QEMU :ref:`Object model <qom>` implementation, with monitor associated commands.
* `replay <https://gitlab.com/qemu-project/qemu/-/tree/master/replay>`_:
  QEMU :ref:`Record/replay <replay>` implementation.
* `roms <https://gitlab.com/qemu-project/qemu/-/tree/master/roms>`_:
  Contains source code for various firmware and ROMs, which can be compiled if
  custom or updated versions are needed.
* `rust <https://gitlab.com/qemu-project/qemu/-/tree/master/rust>`_:
  Rust integration in QEMU. It contains the new interfaces defined and
  associated devices using it.
* `scripts <https://gitlab.com/qemu-project/qemu/-/tree/master/scripts>`_:
  Collection of scripts used in build and test systems, and various
  tools for QEMU codebase and execution traces.
* `scsi <https://gitlab.com/qemu-project/qemu/-/tree/master/scsi>`_:
  Code related to SCSI support, used by SCSI devices.
* `semihosting <https://gitlab.com/qemu-project/qemu/-/tree/master/semihosting>`_:
  QEMU `Semihosting <Semihosting>` implementation.
* `stats <https://gitlab.com/qemu-project/qemu/-/tree/master/stats>`_:
  `Monitor <QEMU monitor>` stats commands implementation.
* `storage-daemon <https://gitlab.com/qemu-project/qemu/-/tree/master/storage-daemon>`_:
  QEMU `Storage daemon <storage-daemon>` implementation.
* `stubs <https://gitlab.com/qemu-project/qemu/-/tree/master/stubs>`_:
  Various stubs (empty functions) used to compile QEMU with specific
  configurations.
* `subprojects <https://gitlab.com/qemu-project/qemu/-/tree/master/subprojects>`_:
  QEMU submodules used by QEMU build system.
* `system <https://gitlab.com/qemu-project/qemu/-/tree/master/system>`_:
  QEMU `system mode <System emulation>` implementation (cpu, mmu, boot support).
* `target <https://gitlab.com/qemu-project/qemu/-/tree/master/target>`_:
  Contains code for all target architectures supported (one subfolder
  per arch). For every architecture, you can find accelerator specific
  implementations.
* `tcg <https://gitlab.com/qemu-project/qemu/-/tree/master/tcg>`_:
  :ref:`TCG <tcg>` related code.
  Contains one subfolder per host supported architecture.
* `tests <https://gitlab.com/qemu-project/qemu/-/tree/master/tests>`_:
  QEMU `test <testing>` suite

  - `data <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/data>`_:
    Data for various tests.
  - `decode <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/decode>`_:
    Testsuite for :ref:`decodetree <decodetree>` implementation.
  - `docker <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/docker>`_:
    Code and scripts to create `containers <container-ref>` used in `CI <ci>`.
  - `fp <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/fp>`_:
    QEMU testsuite for soft float implementation.
  - `functional <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/functional>`_:
    `Functional tests <checkfunctional-ref>` (full VM boot).
  - `lcitool <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/lcitool>`_:
    Generate dockerfiles for CI containers.
  - `migration <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/migration>`_:
    Test scripts and data for :ref:`Migration framework <migration>`.
  - `multiboot <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/multiboot>`_:
    Test multiboot functionality for x86_64/i386.
  - `qapi-schema <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/qapi-schema>`_:
    Test scripts and data for `QAPI <qapi-tests>`.
  - `qemu-iotests <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/qemu-iotests>`_:
    `Disk image and block tests <qemu-iotests>`.
  - `qtest <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/qtest>`_:
    `Device emulation testing <qtest>`.
  - `tcg <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/tcg>`__:
    `TCG related tests <checktcg-ref>`. Contains code per architecture
    (subfolder) and multiarch tests as well.
  - `tsan <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/tsan>`_:
    `Suppressions <tsan-suppressions>` for thread sanitizer.
  - `uefi-test-tools <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/uefi-test-tools>`_:
    Test tool for UEFI support.
  - `unit <https://gitlab.com/qemu-project/qemu/-/tree/master/tests/unit>`_:
    QEMU `Unit tests <unit-tests>`.
* `trace <https://gitlab.com/qemu-project/qemu/-/tree/master/trace>`_:
  :ref:`Tracing framework <tracing>`. Used to print information associated to various
  events during execution.
* `ui <https://gitlab.com/qemu-project/qemu/-/tree/master/ui>`_:
  QEMU User interfaces.
* `util <https://gitlab.com/qemu-project/qemu/-/tree/master/util>`_:
  Utility code used by other parts of QEMU.
