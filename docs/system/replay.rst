.. _replay:

..
    Copyright (c) 2010-2022 Institute for System Programming
                        of the Russian Academy of Sciences.

    This work is licensed under the terms of the GNU GPL, version 2 or later.
    See the COPYING file in the top-level directory.

Record/replay
=============

Record/replay functions are used for the deterministic replay of qemu execution.
Execution recording writes a non-deterministic events log, which can be later
used for replaying the execution anywhere and for unlimited number of times.
It also supports checkpointing for faster rewind to the specific replay moment.
Execution replaying reads the log and replays all non-deterministic events
including external input, hardware clocks, and interrupts.

Deterministic replay has the following features:

 * Deterministically replays whole system execution and all contents of
   the memory, state of the hardware devices, clocks, and screen of the VM.
 * Writes execution log into the file for later replaying for multiple times
   on different machines.
 * Supports i386, x86_64, ARM, AArch64, Risc-V, MIPS, MIPS64, S390X, Alpha,
   PowerPC, PowerPC64, M68000, Microblaze, OpenRISC, Nios II, SPARC,
   and Xtensa hardware platforms.
 * Performs deterministic replay of all operations with keyboard and mouse
   input devices, serial ports, and network.

Usage of the record/replay:

 * First, record the execution with the following command line:

    .. parsed-literal::
        |qemu_system| \\
        -icount shift=auto,rr=record,rrfile=replay.bin \\
        -drive file=disk.qcow2,if=none,snapshot,id=img-direct \\
        -drive driver=blkreplay,if=none,image=img-direct,id=img-blkreplay \\
        -device ide-hd,drive=img-blkreplay \\
        -netdev user,id=net1 -device rtl8139,netdev=net1 \\
        -object filter-replay,id=replay,netdev=net1

 * After recording, you can replay it by using another command line:

    .. parsed-literal::
        |qemu_system| \\
        -icount shift=auto,rr=replay,rrfile=replay.bin \\
        -drive file=disk.qcow2,if=none,snapshot,id=img-direct \\
        -drive driver=blkreplay,if=none,image=img-direct,id=img-blkreplay \\
        -device ide-hd,drive=img-blkreplay \\
        -netdev user,id=net1 -device rtl8139,netdev=net1 \\
        -object filter-replay,id=replay,netdev=net1

   The only difference with recording is changing the rr option
   from record to replay.
 * Block device images are not actually changed in the recording mode,
   because all of the changes are written to the temporary overlay file.
   This behavior is enabled by using blkreplay driver. It should be used
   for every enabled block device, as described in :ref:`block-label` section.
 * ``-net none`` option should be specified when network is not used,
   because QEMU adds network card by default. When network is needed,
   it should be configured explicitly with replay filter, as described
   in :ref:`network-label` section.
 * Interaction with audio devices and serial ports are recorded and replayed
   automatically when such devices are enabled.

Core idea
---------

Record/replay system is based on saving and replaying non-deterministic
events (e.g. keyboard input) and simulating deterministic ones (e.g. reading
from HDD or memory of the VM). Saving only non-deterministic events makes
log file smaller and simulation faster.

The following non-deterministic data from peripheral devices is saved into
the log: mouse and keyboard input, network packets, audio controller input,
serial port input, and hardware clocks (they are non-deterministic
too, because their values are taken from the host machine). Inputs from
simulated hardware, memory of VM, software interrupts, and execution of
instructions are not saved into the log, because they are deterministic and
can be replayed by simulating the behavior of virtual machine starting from
initial state.

Instruction counting
--------------------

QEMU should work in icount mode to use record/replay feature. icount was
designed to allow deterministic execution in absence of external inputs
of the virtual machine. Record/replay feature is enabled through ``-icount``
command-line option, making possible deterministic execution of the machine,
interacting with user or network.

.. _block-label:

Block devices
-------------

Block devices record/replay module intercepts calls of
bdrv coroutine functions at the top of block drivers stack.
To record and replay block operations the drive must be configured
as following:

.. parsed-literal::
    -drive file=disk.qcow2,if=none,snapshot,id=img-direct
    -drive driver=blkreplay,if=none,image=img-direct,id=img-blkreplay
    -device ide-hd,drive=img-blkreplay

blkreplay driver should be inserted between disk image and virtual driver
controller. Therefore all disk requests may be recorded and replayed.

.. _snapshotting-label:

Snapshotting
------------

New VM snapshots may be created in replay mode. They can be used later
to recover the desired VM state. All VM states created in replay mode
are associated with the moment of time in the replay scenario.
After recovering the VM state replay will start from that position.

Default starting snapshot name may be specified with icount field
rrsnapshot as follows:

.. parsed-literal::
    -icount shift=auto,rr=record,rrfile=replay.bin,rrsnapshot=snapshot_name

This snapshot is created at start of recording and restored at start
of replaying. It also can be loaded while replaying to roll back
the execution.

``snapshot`` flag of the disk image must be removed to save the snapshots
in the overlay (or original image) instead of using the temporary overlay.

.. parsed-literal::
    -drive file=disk.ovl,if=none,id=img-direct
    -drive driver=blkreplay,if=none,image=img-direct,id=img-blkreplay
    -device ide-hd,drive=img-blkreplay

Use QEMU monitor to create additional snapshots. ``savevm <name>`` command
created the snapshot and ``loadvm <name>`` restores it. To prevent corruption
of the original disk image, use overlay files linked to the original images.
Therefore all new snapshots (including the starting one) will be saved in
overlays and the original image remains unchanged.

When you need to use snapshots with diskless virtual machine,
it must be started with "orphan" qcow2 image. This image will be used
for storing VM snapshots. Here is the example of the command line for this:

.. parsed-literal::
    |qemu_system| \\
      -icount shift=auto,rr=replay,rrfile=record.bin,rrsnapshot=init \\
      -net none -drive file=empty.qcow2,if=none,id=rr

``empty.qcow2`` drive does not connected to any virtual block device and used
for VM snapshots only.

.. _network-label:

Network devices
---------------

Record and replay for network interactions is performed with the network filter.
Each backend must have its own instance of the replay filter as follows:

.. parsed-literal::
    -netdev user,id=net1 -device rtl8139,netdev=net1
    -object filter-replay,id=replay,netdev=net1

Replay network filter is used to record and replay network packets. While
recording the virtual machine this filter puts all packets coming from
the outer world into the log. In replay mode packets from the log are
injected into the network device. All interactions with network backend
in replay mode are disabled.

Audio devices
-------------

Audio data is recorded and replay automatically. The command line for recording
and replaying must contain identical specifications of audio hardware, e.g.:

.. parsed-literal::
    -soundhw ac97

Serial ports
------------

Serial ports input is recorded and replay automatically. The command lines
for recording and replaying must contain identical number of ports in record
and replay modes, but their backends may differ.
E.g., ``-serial stdio`` in record mode, and ``-serial null`` in replay mode.

Reverse debugging
-----------------

Reverse debugging allows "executing" the program in reverse direction.
GDB remote protocol supports "reverse step" and "reverse continue"
commands. The first one steps single instruction backwards in time,
and the second one finds the last breakpoint in the past.

Recorded executions may be used to enable reverse debugging. QEMU can't
execute the code in backwards direction, but can load a snapshot and
replay forward to find the desired position or breakpoint.

The following GDB commands are supported:

 - ``reverse-stepi`` (or ``rsi``) - step one instruction backwards
 - ``reverse-continue`` (or ``rc``) - find last breakpoint in the past

Reverse step loads the nearest snapshot and replays the execution until
the required instruction is met.

Reverse continue may include several passes of examining the execution
between the snapshots. Each of the passes include the following steps:

 #. loading the snapshot
 #. replaying to examine the breakpoints
 #. if breakpoint or watchpoint was met

    * loading the snapshot again
    * replaying to the required breakpoint

 #. else

    * proceeding to the p.1 with the earlier snapshot

Therefore usage of the reverse debugging requires at least one snapshot
created. This can be done by omitting ``snapshot`` option
for the block drives and adding ``rrsnapshot`` for both record and replay
command lines.
See the :ref:`snapshotting-label` section to learn more about running record/replay
and creating the snapshot in these modes.

When ``rrsnapshot`` is not used, then snapshot named ``start_debugging``
created in temporary overlay. This allows using reverse debugging, but with
temporary snapshots (existing within the session).
