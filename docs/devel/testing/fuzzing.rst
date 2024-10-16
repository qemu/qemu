========
Fuzzing
========

This document describes the virtual-device fuzzing infrastructure in QEMU and
how to use it to implement additional fuzzers.

Basics
------

Fuzzing operates by passing inputs to an entry point/target function. The
fuzzer tracks the code coverage triggered by the input. Based on these
findings, the fuzzer mutates the input and repeats the fuzzing.

To fuzz QEMU, we rely on libfuzzer. Unlike other fuzzers such as AFL, libfuzzer
is an *in-process* fuzzer. For the developer, this means that it is their
responsibility to ensure that state is reset between fuzzing-runs.

Building the fuzzers
--------------------

To build the fuzzers, install a recent version of clang:
Configure with (substitute the clang binaries with the version you installed).
Here, enable-asan and enable-ubsan are optional but they allow us to reliably
detect bugs such as out-of-bounds accesses, uses-after-free, double-frees
etc.::

    CC=clang-8 CXX=clang++-8 /path/to/configure \
        --enable-fuzzing --enable-asan --enable-ubsan

Fuzz targets are built similarly to system targets::

    make qemu-fuzz-i386

This builds ``./qemu-fuzz-i386``

The first option to this command is: ``--fuzz-target=FUZZ_NAME``
To list all of the available fuzzers run ``qemu-fuzz-i386`` with no arguments.

For example::

    ./qemu-fuzz-i386 --fuzz-target=virtio-scsi-fuzz

Internally, libfuzzer parses all arguments that do not begin with ``"--"``.
Information about these is available by passing ``-help=1``

Now the only thing left to do is wait for the fuzzer to trigger potential
crashes.

Useful libFuzzer flags
----------------------

As mentioned above, libFuzzer accepts some arguments. Passing ``-help=1`` will
list the available arguments. In particular, these arguments might be helpful:

* ``CORPUS_DIR/`` : Specify a directory as the last argument to libFuzzer.
  libFuzzer stores each "interesting" input in this corpus directory. The next
  time you run libFuzzer, it will read all of the inputs from the corpus, and
  continue fuzzing from there. You can also specify multiple directories.
  libFuzzer loads existing inputs from all specified directories, but will only
  write new ones to the first one specified.

* ``-max_len=4096`` : specify the maximum byte-length of the inputs libFuzzer
  will generate.

* ``-close_fd_mask={1,2,3}`` : close, stderr, or both. Useful for targets that
  trigger many debug/error messages, or create output on the serial console.

* ``-jobs=4 -workers=4`` : These arguments configure libFuzzer to run 4 fuzzers in
  parallel (4 fuzzing jobs in 4 worker processes). Alternatively, with only
  ``-jobs=N``, libFuzzer automatically spawns a number of workers less than or equal
  to half the available CPU cores. Replace 4 with a number appropriate for your
  machine. Make sure to specify a ``CORPUS_DIR``, which will allow the parallel
  fuzzers to share information about the interesting inputs they find.

* ``-use_value_profile=1`` : For each comparison operation, libFuzzer computes
  ``(caller_pc&4095) | (popcnt(Arg1 ^ Arg2) << 12)`` and places this in the
  coverage table. Useful for targets with "magic" constants. If Arg1 came from
  the fuzzer's input and Arg2 is a magic constant, then each time the Hamming
  distance between Arg1 and Arg2 decreases, libFuzzer adds the input to the
  corpus.

* ``-shrink=1`` : Tries to make elements of the corpus "smaller". Might lead to
  better coverage performance, depending on the target.

Note that libFuzzer's exact behavior will depend on the version of
clang and libFuzzer used to build the device fuzzers.

Generating Coverage Reports
---------------------------

Code coverage is a crucial metric for evaluating a fuzzer's performance.
libFuzzer's output provides a "cov: " column that provides a total number of
unique blocks/edges covered. To examine coverage on a line-by-line basis we
can use Clang coverage:

 1. Configure libFuzzer to store a corpus of all interesting inputs (see
    CORPUS_DIR above)
 2. ``./configure`` the QEMU build with ::

    --enable-fuzzing \
    --extra-cflags="-fprofile-instr-generate -fcoverage-mapping"

 3. Re-run the fuzzer. Specify $CORPUS_DIR/* as an argument, telling libfuzzer
    to execute all of the inputs in $CORPUS_DIR and exit. Once the process
    exits, you should find a file, "default.profraw" in the working directory.
 4. Execute these commands to generate a detailed HTML coverage-report::

      llvm-profdata merge -output=default.profdata default.profraw
      llvm-cov show ./path/to/qemu-fuzz-i386 -instr-profile=default.profdata \
      --format html -output-dir=/path/to/output/report

Adding a new fuzzer
-------------------

Coverage over virtual devices can be improved by adding additional fuzzers.
Fuzzers are kept in ``tests/qtest/fuzz/`` and should be added to
``tests/qtest/fuzz/meson.build``

Fuzzers can rely on both qtest and libqos to communicate with virtual devices.

1. Create a new source file. For example ``tests/qtest/fuzz/foo-device-fuzz.c``.

2. Write the fuzzing code using the libqtest/libqos API. See existing fuzzers
   for reference.

3. Add the fuzzer to ``tests/qtest/fuzz/meson.build``.

Fuzzers can be more-or-less thought of as special qtest programs which can
modify the qtest commands and/or qtest command arguments based on inputs
provided by libfuzzer. Libfuzzer passes a byte array and length. Commonly the
fuzzer loops over the byte-array interpreting it as a list of qtest commands,
addresses, or values.

The Generic Fuzzer
------------------

Writing a fuzz target can be a lot of effort (especially if a device driver has
not be built-out within libqos). Many devices can be fuzzed to some degree,
without any device-specific code, using the generic-fuzz target.

The generic-fuzz target is capable of fuzzing devices over their PIO, MMIO,
and DMA input-spaces. To apply the generic-fuzz to a device, we need to define
two env-variables, at minimum:

* ``QEMU_FUZZ_ARGS=`` is the set of QEMU arguments used to configure a machine, with
  the device attached. For example, if we want to fuzz the virtio-net device
  attached to a pc-i440fx machine, we can specify::

    QEMU_FUZZ_ARGS="-M pc -nodefaults -netdev user,id=user0 \
    -device virtio-net,netdev=user0"

* ``QEMU_FUZZ_OBJECTS=`` is a set of space-delimited strings used to identify
  the MemoryRegions that will be fuzzed. These strings are compared against
  MemoryRegion names and MemoryRegion owner names, to decide whether each
  MemoryRegion should be fuzzed. These strings support globbing. For the
  virtio-net example, we could use one of ::

    QEMU_FUZZ_OBJECTS='virtio-net'
    QEMU_FUZZ_OBJECTS='virtio*'
    QEMU_FUZZ_OBJECTS='virtio* pcspk' # Fuzz the virtio devices and the speaker
    QEMU_FUZZ_OBJECTS='*' # Fuzz the whole machine``

The ``"info mtree"`` and ``"info qom-tree"`` monitor commands can be especially
useful for identifying the ``MemoryRegion`` and ``Object`` names used for
matching.

As a generic rule-of-thumb, the more ``MemoryRegions``/Devices we match, the
greater the input-space, and the smaller the probability of finding crashing
inputs for individual devices. As such, it is usually a good idea to limit the
fuzzer to only a few ``MemoryRegions``.

To ensure that these env variables have been configured correctly, we can use::

    ./qemu-fuzz-i386 --fuzz-target=generic-fuzz -runs=0

The output should contain a complete list of matched MemoryRegions.

OSS-Fuzz
--------
QEMU is continuously fuzzed on `OSS-Fuzz
<https://github.com/google/oss-fuzz>`_.  By default, the OSS-Fuzz build
will try to fuzz every fuzz-target. Since the generic-fuzz target
requires additional information provided in environment variables, we
pre-define some generic-fuzz configs in
``tests/qtest/fuzz/generic_fuzz_configs.h``. Each config must specify:

- ``.name``: To identify the fuzzer config

- ``.args`` OR ``.argfunc``: A string or pointer to a function returning a
  string.  These strings are used to specify the ``QEMU_FUZZ_ARGS``
  environment variable.  ``argfunc`` is useful when the config relies on e.g.
  a dynamically created temp directory, or a free tcp/udp port.

- ``.objects``: A string that specifies the ``QEMU_FUZZ_OBJECTS`` environment
  variable.

To fuzz additional devices/device configuration on OSS-Fuzz, send patches for
either a new device-specific fuzzer or a new generic-fuzz config.

Build details:

- The Dockerfile that sets up the environment for building QEMU's
  fuzzers on OSS-Fuzz can be fund in the OSS-Fuzz repository
  __(https://github.com/google/oss-fuzz/blob/master/projects/qemu/Dockerfile)

- The script responsible for building the fuzzers can be found in the
  QEMU source tree at ``scripts/oss-fuzz/build.sh``

Building Crash Reproducers
-----------------------------------------
When we find a crash, we should try to create an independent reproducer, that
can be used on a non-fuzzer build of QEMU. This filters out any potential
false-positives, and improves the debugging experience for developers.
Here are the steps for building a reproducer for a crash found by the
generic-fuzz target.

- Ensure the crash reproduces::

    qemu-fuzz-i386 --fuzz-target... ./crash-...

- Gather the QTest output for the crash::

    QEMU_FUZZ_TIMEOUT=0 QTEST_LOG=1 FUZZ_SERIALIZE_QTEST=1 \
    qemu-fuzz-i386 --fuzz-target... ./crash-... &> /tmp/trace

- Reorder and clean-up the resulting trace::

    scripts/oss-fuzz/reorder_fuzzer_qtest_trace.py /tmp/trace > /tmp/reproducer

- Get the arguments needed to start qemu, and provide a path to qemu::

    less /tmp/trace # The args should be logged at the top of this file
    export QEMU_ARGS="-machine ..."
    export QEMU_PATH="path/to/qemu-system"

- Ensure the crash reproduces in qemu-system::

    $QEMU_PATH $QEMU_ARGS -qtest stdio < /tmp/reproducer

- From the crash output, obtain some string that identifies the crash. This
  can be a line in the stack-trace, for example::

    export CRASH_TOKEN="hw/usb/hcd-xhci.c:1865"

- Minimize the reproducer::

    scripts/oss-fuzz/minimize_qtest_trace.py -M1 -M2 \
      /tmp/reproducer /tmp/reproducer-minimized

- Confirm that the minimized reproducer still crashes::

    $QEMU_PATH $QEMU_ARGS -qtest stdio < /tmp/reproducer-minimized

- Create a one-liner reproducer that can be sent over email::

    ./scripts/oss-fuzz/output_reproducer.py -bash /tmp/reproducer-minimized

- Output the C source code for a test case that will reproduce the bug::

    ./scripts/oss-fuzz/output_reproducer.py -owner "John Smith <john@smith.com>"\
      -name "test_function_name" /tmp/reproducer-minimized

- Report the bug and send a patch with the C reproducer upstream

Implementation Details / Fuzzer Lifecycle
-----------------------------------------

The fuzzer has two entrypoints that libfuzzer calls. libfuzzer provides it's
own ``main()``, which performs some setup, and calls the entrypoints:

``LLVMFuzzerInitialize``: called prior to fuzzing. Used to initialize all of the
necessary state

``LLVMFuzzerTestOneInput``: called for each fuzzing run. Processes the input and
resets the state at the end of each run.

In more detail:

``LLVMFuzzerInitialize`` parses the arguments to the fuzzer (must start with two
dashes, so they are ignored by libfuzzer ``main()``). Currently, the arguments
select the fuzz target. Then, the qtest client is initialized. If the target
requires qos, qgraph is set up and the QOM/LIBQOS modules are initialized.
Then the QGraph is walked and the QEMU cmd_line is determined and saved.

After this, the ``vl.c:main`` is called to set up the guest. There are
target-specific hooks that can be called before and after main, for
additional setup(e.g. PCI setup, or VM snapshotting).

``LLVMFuzzerTestOneInput``: Uses qtest/qos functions to act based on the fuzz
input. It is also responsible for manually calling ``main_loop_wait`` to ensure
that bottom halves are executed and any cleanup required before the next input.

Since the same process is reused for many fuzzing runs, QEMU state needs to
be reset at the end of each run. For example, this can be done by rebooting the
VM, after each run.

  - *Pros*: Straightforward and fast for simple fuzz targets.

  - *Cons*: Depending on the device, does not reset all device state. If the
    device requires some initialization prior to being ready for fuzzing (common
    for QOS-based targets), this initialization needs to be done after each
    reboot.

  - *Example target*: ``i440fx-qtest-reboot-fuzz``
