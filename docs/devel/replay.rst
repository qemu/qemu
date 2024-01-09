..
   Copyright (c) 2022, ISP RAS
   Written by Pavel Dovgalyuk and Alex Bennée

=======================
Execution Record/Replay
=======================

Core concepts
=============

Record/replay functions are used for the deterministic replay of qemu
execution. Execution recording writes a non-deterministic events log, which
can be later used for replaying the execution anywhere and for unlimited
number of times. Execution replaying reads the log and replays all
non-deterministic events including external input, hardware clocks,
and interrupts.

Several parts of QEMU include function calls to make event log recording
and replaying.
Devices' models that have non-deterministic input from external devices were
changed to write every external event into the execution log immediately.
E.g. network packets are written into the log when they arrive into the virtual
network adapter.

All non-deterministic events are coming from these devices. But to
replay them we need to know at which moments they occur. We specify
these moments by counting the number of instructions executed between
every pair of consecutive events.

Academic papers with description of deterministic replay implementation:

* `Deterministic Replay of System's Execution with Multi-target QEMU Simulator for Dynamic Analysis and Reverse Debugging <https://www.computer.org/csdl/proceedings/csmr/2012/4666/00/4666a553-abs.html>`_
* `Don't panic: reverse debugging of kernel drivers <https://dl.acm.org/citation.cfm?id=2786805.2803179>`_

Modifications of qemu include:

 * wrappers for clock and time functions to save their return values in the log
 * saving different asynchronous events (e.g. system shutdown) into the log
 * synchronization of the bottom halves execution
 * synchronization of the threads from thread pool
 * recording/replaying user input (mouse, keyboard, and microphone)
 * adding internal checkpoints for cpu and io synchronization
 * network filter for recording and replaying the packets
 * block driver for making block layer deterministic
 * serial port input record and replay
 * recording of random numbers obtained from the external sources

Instruction counting
--------------------

QEMU should work in icount mode to use record/replay feature. icount was
designed to allow deterministic execution in absence of external inputs
of the virtual machine. We also use icount to control the occurrence of the
non-deterministic events. The number of instructions elapsed from the last event
is written to the log while recording the execution. In replay mode we
can predict when to inject that event using the instruction counter.

Locking and thread synchronisation
----------------------------------

Previously the synchronisation of the main thread and the vCPU thread
was ensured by the holding of the BQL. However the trend has been to
reduce the time the BQL was held across the system including under TCG
system emulation. As it is important that batches of events are kept
in sequence (e.g. expiring timers and checkpoints in the main thread
while instruction checkpoints are written by the vCPU thread) we need
another lock to keep things in lock-step. This role is now handled by
the replay_mutex_lock. It used to be held only for each event being
written but now it is held for a whole execution period. This results
in a deterministic ping-pong between the two main threads.

As the BQL is now a finer grained lock than the replay_lock it is almost
certainly a bug, and a source of deadlocks, to take the
replay_mutex_lock while the BQL is held. This is enforced by an assert.
While the unlocks are usually in the reverse order, this is not
necessary; you can drop the replay_lock while holding the BQL, without
doing a more complicated unlock_iothread/replay_unlock/lock_iothread
sequence.

Checkpoints
-----------

Replaying the execution of virtual machine is bound by sources of
non-determinism. These are inputs from clock and peripheral devices,
and QEMU thread scheduling. Thread scheduling affect on processing events
from timers, asynchronous input-output, and bottom halves.

Invocations of timers are coupled with clock reads and changing the state
of the virtual machine. Reads produce non-deterministic data taken from
host clock. And VM state changes should preserve their order. Their relative
order in replay mode must replicate the order of callbacks in record mode.
To preserve this order we use checkpoints. When a specific clock is processed
in record mode we save to the log special "checkpoint" event.
Checkpoints here do not refer to virtual machine snapshots. They are just
record/replay events used for synchronization.

QEMU in replay mode will try to invoke timers processing in random moment
of time. That's why we do not process a group of timers until the checkpoint
event will be read from the log. Such an event allows synchronizing CPU
execution and timer events.

Two other checkpoints govern the "warping" of the virtual clock.
While the virtual machine is idle, the virtual clock increments at
1 ns per *real time* nanosecond.  This is done by setting up a timer
(called the warp timer) on the virtual real time clock, so that the
timer fires at the next deadline of the virtual clock; the virtual clock
is then incremented (which is called "warping" the virtual clock) as
soon as the timer fires or the CPUs need to go out of the idle state.
Two functions are used for this purpose; because these actions change
virtual machine state and must be deterministic, each of them creates a
checkpoint. ``icount_start_warp_timer`` checks if the CPUs are idle and if so
starts accounting real time to virtual clock. ``icount_account_warp_timer``
is called when the CPUs get an interrupt or when the warp timer fires,
and it warps the virtual clock by the amount of real time that has passed
since ``icount_start_warp_timer``.

Virtual devices
===============

Record/replay mechanism, that could be enabled through icount mode, expects
the virtual devices to satisfy the following requirement:
everything that affects
the guest state during execution in icount mode should be deterministic.

Timers
------

Timers are used to execute callbacks from different subsystems of QEMU
at the specified moments of time. There are several kinds of timers:

 * Real time clock. Based on host time and used only for callbacks that
   do not change the virtual machine state. For this reason real time
   clock and timers does not affect deterministic replay at all.
 * Virtual clock. These timers run only during the emulation. In icount
   mode virtual clock value is calculated using executed instructions counter.
   That is why it is completely deterministic and does not have to be recorded.
 * Host clock. This clock is used by device models that simulate real time
   sources (e.g. real time clock chip). Host clock is the one of the sources
   of non-determinism. Host clock read operations should be logged to
   make the execution deterministic.
 * Virtual real time clock. This clock is similar to real time clock but
   it is used only for increasing virtual clock while virtual machine is
   sleeping. Due to its nature it is also non-deterministic as the host clock
   and has to be logged too.

All virtual devices should use virtual clock for timers that change the guest
state. Virtual clock is deterministic, therefore such timers are deterministic
too.

Virtual devices can also use realtime clock for the events that do not change
the guest state directly. When the clock ticking should depend on VM execution
speed, use virtual clock with EXTERNAL attribute. It is not deterministic,
but its speed depends on the guest execution. This clock is used by
the virtual devices (e.g., slirp routing device) that lie outside the
replayed guest.

Block devices
-------------

Block devices record/replay module (``blkreplay``) intercepts calls of
bdrv coroutine functions at the top of block drivers stack.

All block completion operations are added to the queue in the coroutines.
When the queue is flushed the information about processed requests
is recorded to the log. In replay phase the queue is matched with
events read from the log. Therefore block devices requests are processed
deterministically.

Bottom halves
-------------

Bottom half callbacks, that affect the guest state, should be invoked through
``replay_bh_schedule_event`` or ``replay_bh_schedule_oneshot_event`` functions.
Their invocations are saved in record mode and synchronized with the existing
log in replay mode.

Disk I/O events are completely deterministic in our model, because
in both record and replay modes we start virtual machine from the same
disk state. But callbacks that virtual disk controller uses for reading and
writing the disk may occur at different moments of time in record and replay
modes.

Reading and writing requests are created by CPU thread of QEMU. Later these
requests proceed to block layer which creates "bottom halves". Bottom
halves consist of callback and its parameters. They are processed when
main loop locks the BQL. These locks are not synchronized with
replaying process because main loop also processes the events that do not
affect the virtual machine state (like user interaction with monitor).

That is why we had to implement saving and replaying bottom halves callbacks
synchronously to the CPU execution. When the callback is about to execute
it is added to the queue in the replay module. This queue is written to the
log when its callbacks are executed. In replay mode callbacks are not processed
until the corresponding event is read from the events log file.

Sometimes the block layer uses asynchronous callbacks for its internal purposes
(like reading or writing VM snapshots or disk image cluster tables). In this
case bottom halves are not marked as "replayable" and do not saved
into the log.

Saving/restoring the VM state
-----------------------------

All fields in the device state structure (including virtual timers)
should be restored by loadvm to the same values they had before savevm.

Avoid accessing other devices' state, because the order of saving/restoring
is not defined. It means that you should not call functions like
``update_irq`` in ``post_load`` callback. Save everything explicitly to avoid
the dependencies that may make restoring the VM state non-deterministic.

Stopping the VM
---------------

Stopping the guest should not interfere with its state (with the exception
of the network connections, that could be broken by the remote timeouts).
VM can be stopped at any moment of replay by the user. Restarting the VM
after that stop should not break the replay by the unneeded guest state change.

Replay log format
=================

Record/replay log consists of the header and the sequence of execution
events. The header includes 4-byte replay version id and 8-byte reserved
field. Version is updated every time replay log format changes to prevent
using replay log created by another build of qemu.

The sequence of the events describes virtual machine state changes.
It includes all non-deterministic inputs of VM, synchronization marks and
instruction counts used to correctly inject inputs at replay.

Synchronization marks (checkpoints) are used for synchronizing qemu threads
that perform operations with virtual hardware. These operations may change
system's state (e.g., change some register or generate interrupt) and
therefore should execute synchronously with CPU thread.

Every event in the log includes 1-byte event id and optional arguments.
When argument is an array, it is stored as 4-byte array length
and corresponding number of bytes with data.
Here is the list of events that are written into the log:

 - EVENT_INSTRUCTION. Instructions executed since last event. Followed by:

   - 4-byte number of executed instructions.

 - EVENT_INTERRUPT. Used to synchronize interrupt processing.
 - EVENT_EXCEPTION. Used to synchronize exception handling.
 - EVENT_ASYNC. This is a group of events. When such an event is generated,
   it is stored in the queue and processed in icount_account_warp_timer().
   Every such event has it's own id from the following list:

     - REPLAY_ASYNC_EVENT_BH. Bottom-half callback. This event synchronizes
       callbacks that affect virtual machine state, but normally called
       asynchronously. Followed by:

        - 8-byte operation id.

     - REPLAY_ASYNC_EVENT_INPUT. Input device event. Contains
       parameters of keyboard and mouse input operations
       (key press/release, mouse pointer movement). Followed by:

        - 9-16 bytes depending of input event.

     - REPLAY_ASYNC_EVENT_INPUT_SYNC. Internal input synchronization event.
     - REPLAY_ASYNC_EVENT_CHAR_READ. Character (e.g., serial port) device input
       initiated by the sender. Followed by:

        - 1-byte character device id.
        - Array with bytes were read.

     - REPLAY_ASYNC_EVENT_BLOCK. Block device operation. Used to synchronize
       operations with disk and flash drives with CPU. Followed by:

        - 8-byte operation id.

     - REPLAY_ASYNC_EVENT_NET. Incoming network packet. Followed by:

        - 1-byte network adapter id.
        - 4-byte packet flags.
        - Array with packet bytes.

 - EVENT_SHUTDOWN. Occurs when user sends shutdown event to qemu,
   e.g., by closing the window.
 - EVENT_CHAR_WRITE. Used to synchronize character output operations. Followed by:

    - 4-byte output function return value.
    - 4-byte offset in the output array.

 - EVENT_CHAR_READ_ALL. Used to synchronize character input operations,
   initiated by qemu. Followed by:

    - Array with bytes that were read.

 - EVENT_CHAR_READ_ALL_ERROR. Unsuccessful character input operation,
   initiated by qemu. Followed by:

    - 4-byte error code.

 - EVENT_CLOCK + clock_id. Group of events for host clock read operations. Followed by:

    - 8-byte clock value.

 - EVENT_CHECKPOINT + checkpoint_id. Checkpoint for synchronization of
   CPU, internal threads, and asynchronous input events.
 - EVENT_END. Last event in the log.
