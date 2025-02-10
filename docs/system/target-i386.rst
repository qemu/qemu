.. _QEMU-PC-System-emulator:

x86 System emulator
-------------------

Board-specific documentation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

..
   This table of contents should be kept sorted alphabetically
   by the title text of each file, which isn't the same ordering
   as an alphabetical sort by filename.

.. toctree::
   :maxdepth: 1

   i386/pc
   i386/microvm
   i386/nitro-enclave

Architectural features
~~~~~~~~~~~~~~~~~~~~~~

.. toctree::
   :maxdepth: 1

   i386/cpu
   i386/hyperv
   i386/xen
   i386/xenpvh
   i386/kvm-pv
   i386/sgx
   i386/amd-memory-encryption

OS requirements
~~~~~~~~~~~~~~~

On x86_64 hosts, the default set of CPU features enabled by the KVM
accelerator require the host to be running Linux v4.5 or newer. Red Hat
Enterprise Linux 7 is also supported, since the required
functionality was backported.
