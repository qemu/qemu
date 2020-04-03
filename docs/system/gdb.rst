.. _gdb_005fusage:

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
connection, use the ``-gdb dev`` option instead of ``-s``.)

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

Advanced debugging options:

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
