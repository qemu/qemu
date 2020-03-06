.. _pcsys_005fmonitor:

QEMU Monitor
------------

The QEMU monitor is used to give complex commands to the QEMU emulator.
You can use it to:

-  Remove or insert removable media images (such as CD-ROM or
   floppies).

-  Freeze/unfreeze the Virtual Machine (VM) and save or restore its
   state from a disk file.

-  Inspect the VM state without an external debugger.

Commands
~~~~~~~~

The following commands are available:

.. hxtool-doc:: hmp-commands.hx

.. hxtool-doc:: hmp-commands-info.hx

Integer expressions
~~~~~~~~~~~~~~~~~~~

The monitor understands integers expressions for every integer argument.
You can use register names to get the value of specifics CPU registers
by prefixing them with *$*.
