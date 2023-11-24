.. _GDB usage:

GDB usage
---------

QEMU supports working with gdb via gdb's remote-connection facility
(the "gdbstub"). This allows you to debug guest code in the same
way that you might with a low-level debug facility like JTAG
on real hardware. You can stop and start the virtual machine,
examine state like registers and memory, and set breakpoints and
watchpoints.

In order to use gdb, launch QEMU with the ``-s`` and ``-S`` options.
The ``-s`` option will make QEMU listen for an incoming connection
from gdb on TCP port 1234, and ``-S`` will make QEMU not start the
guest until you tell it to from gdb. (If you want to specify which
TCP port to use or to use something other than TCP for the gdbstub
connection, use the ``-gdb dev`` option instead of ``-s``. See
`Using unix sockets`_ for an example.)

.. parsed-literal::

   |qemu_system| -s -S -kernel bzImage -hda rootdisk.img -append "root=/dev/hda"

QEMU will launch but will silently wait for gdb to connect.

Then launch gdb on the 'vmlinux' executable::

   > gdb vmlinux

In gdb, connect to QEMU::

   (gdb) target remote localhost:1234

Then you can use gdb normally. For example, type 'c' to launch the
kernel::

   (gdb) c

Here are some useful tips in order to use gdb on system code:

1. Use ``info reg`` to display all the CPU registers.

2. Use ``x/10i $eip`` to display the code at the PC position.

3. Use ``set architecture i8086`` to dump 16 bit code. Then use
   ``x/10i $cs*16+$eip`` to dump the code at the PC position.

Breakpoint and Watchpoint support
=================================

While GDB can always fall back to inserting breakpoints into memory
(if writable) other features are very much dependent on support of the
accelerator. For TCG system emulation we advertise an infinite number
of hardware assisted breakpoints and watchpoints. For other
accelerators it will depend on if support has been added (see
supports_guest_debug and related hooks in AccelOpsClass).

As TCG cannot track all memory accesses in user-mode there is no
support for watchpoints.

Relocating code
===============

On modern kernels confusion can be caused by code being relocated by
features such as address space layout randomisation. To avoid
confusion when debugging such things you either need to update gdb's
view of where things are in memory or perhaps more trivially disable
ASLR when booting the system.

Debugging user-space in system emulation
========================================

While it is technically possible to debug a user-space program running
inside a system image, it does present challenges. Kernel preemption
and execution mode changes between kernel and user mode can make it
hard to follow what's going on. Unless you are specifically trying to
debug some interaction between kernel and user-space you are better
off running your guest program with gdb either in the guest or using
a gdbserver exposed via a port to the outside world.

Debugging multicore machines
============================

GDB's abstraction for debugging targets with multiple possible
parallel flows of execution is a two layer one: it supports multiple
"inferiors", each of which can have multiple "threads". When the QEMU
machine has more than one CPU, QEMU exposes each CPU cluster as a
separate "inferior", where each CPU within the cluster is a separate
"thread". Most QEMU machine types have identical CPUs, so there is a
single cluster which has all the CPUs in it.  A few machine types are
heterogeneous and have multiple clusters: for example the ``sifive_u``
machine has a cluster with one E51 core and a second cluster with four
U54 cores. Here the E51 is the only thread in the first inferior, and
the U54 cores are all threads in the second inferior.

When you connect gdb to the gdbstub, it will automatically
connect to the first inferior; you can display the CPUs in this
cluster using the gdb ``info thread`` command, and switch between
them using gdb's usual thread-management commands.

For multi-cluster machines, unfortunately gdb does not by default
handle multiple inferiors, and so you have to explicitly connect
to them. First, you must connect with the ``extended-remote``
protocol, not ``remote``::

    (gdb) target extended-remote localhost:1234

Once connected, gdb will have a single inferior, for the
first cluster. You need to create inferiors for the other
clusters and attach to them, like this::

  (gdb) add-inferior
  Added inferior 2
  (gdb) inferior 2
  [Switching to inferior 2 [<null>] (<noexec>)]
  (gdb) attach 2
  Attaching to process 2
  warning: No executable has been specified and target does not support
  determining executable automatically.  Try using the "file" command.
  0x00000000 in ?? ()

Once you've done this, ``info threads`` will show CPUs in
all the clusters you have attached to::

  (gdb) info threads
    Id   Target Id         Frame
    1.1  Thread 1.1 (cortex-m33-arm-cpu cpu [running]) 0x00000000 in ?? ()
  * 2.1  Thread 2.2 (cortex-m33-arm-cpu cpu [halted ]) 0x00000000 in ?? ()

You probably also want to set gdb to ``schedule-multiple`` mode,
so that when you tell gdb to ``continue`` it resumes all CPUs,
not just those in the cluster you are currently working on::

  (gdb) set schedule-multiple on

Using unix sockets
==================

An alternate method for connecting gdb to the QEMU gdbstub is to use
a unix socket (if supported by your operating system). This is useful when
running several tests in parallel, or if you do not have a known free TCP
port (e.g. when running automated tests).

First create a chardev with the appropriate options, then
instruct the gdbserver to use that device:

.. parsed-literal::

   |qemu_system| -chardev socket,path=/tmp/gdb-socket,server=on,wait=off,id=gdb0 -gdb chardev:gdb0 -S ...

Start gdb as before, but this time connect using the path to
the socket::

   (gdb) target remote /tmp/gdb-socket

Note that to use a unix socket for the connection you will need
gdb version 9.0 or newer.

Advanced debugging options
==========================

Changing single-stepping behaviour
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The default single stepping behavior is step with the IRQs and timer
service routines off. It is set this way because when gdb executes a
single step it expects to advance beyond the current instruction. With
the IRQs and timer service routines on, a single step might jump into
the one of the interrupt or exception vectors instead of executing the
current instruction. This means you may hit the same breakpoint a number
of times before executing the instruction gdb wants to have executed.
Because there are rare circumstances where you want to single step into
an interrupt vector the behavior can be controlled from GDB. There are
three commands you can query and set the single step behavior:

``maintenance packet qqemu.sstepbits``
   This will display the MASK bits used to control the single stepping
   IE:

   ::

      (gdb) maintenance packet qqemu.sstepbits
      sending: "qqemu.sstepbits"
      received: "ENABLE=1,NOIRQ=2,NOTIMER=4"

``maintenance packet qqemu.sstep``
   This will display the current value of the mask used when single
   stepping IE:

   ::

      (gdb) maintenance packet qqemu.sstep
      sending: "qqemu.sstep"
      received: "0x7"

``maintenance packet Qqemu.sstep=HEX_VALUE``
   This will change the single step mask, so if wanted to enable IRQs on
   the single step, but not timers, you would use:

   ::

      (gdb) maintenance packet Qqemu.sstep=0x5
      sending: "qemu.sstep=0x5"
      received: "OK"

Examining physical memory
^^^^^^^^^^^^^^^^^^^^^^^^^

Another feature that QEMU gdbstub provides is to toggle the memory GDB
works with, by default GDB will show the current process memory respecting
the virtual address translation.

If you want to examine/change the physical memory you can set the gdbstub
to work with the physical memory rather with the virtual one.

The memory mode can be checked by sending the following command:

``maintenance packet qqemu.PhyMemMode``
    This will return either 0 or 1, 1 indicates you are currently in the
    physical memory mode.

``maintenance packet Qqemu.PhyMemMode:1``
    This will change the memory mode to physical memory.

``maintenance packet Qqemu.PhyMemMode:0``
    This will change it back to normal memory mode.

Security considerations
=======================

Connecting to the GDB socket allows running arbitrary code inside the guest;
in case of the TCG emulation, which is not considered a security boundary, this
also means running arbitrary code on the host. Additionally, when debugging
qemu-user, it allows directly downloading any file readable by QEMU from the
host.

The GDB socket is not protected by authentication, authorization or encryption.
It is therefore a responsibility of the user to make sure that only authorized
clients can connect to it, e.g., by using a unix socket with proper
permissions, or by opening a TCP socket only on interfaces that are not
reachable by potential attackers.
