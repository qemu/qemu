.. _pcsys_005fquickstart:

Quick Start
-----------

Download and uncompress a PC hard disk image with Linux installed (e.g.
``linux.img``) and type:

.. parsed-literal::

   |qemu_system| linux.img

Linux should boot and give you a prompt.

Users should be aware the above example elides a lot of the complexity
of setting up a VM with x86_64 specific defaults and assumes the
first non switch argument is a PC compatible disk image with a boot
sector. For a non-x86 system where we emulate a broad range of machine
types, the command lines are generally more explicit in defining the
machine and boot behaviour. You will find more example command lines
in the :ref:`system-targets-ref` section of the manual.
