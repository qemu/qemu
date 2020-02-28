:orphan:

..
   This file is the skeleton for the qemu.1 manpage. It mostly
   should simply include the .rst.inc files corresponding to the
   parts of the documentation that go in the manpage as well as the
   HTML manual.

Title
=====

Synopsis
--------

.. parsed-literal::

   |qemu_system| [options] [disk_image]

Description
-----------

.. include:: target-i386-desc.rst.inc

Options
-------

disk_image is a raw hard disk image for IDE hard disk 0. Some targets do
not need a disk image.

.. hxtool-doc:: qemu-options.hx

.. include:: keys.rst.inc

.. include:: mux-chardev.rst.inc

Notes
-----

.. include:: device-url-syntax.rst.inc

See also
--------

The HTML documentation of QEMU for more precise information and Linux
user mode emulator invocation.
