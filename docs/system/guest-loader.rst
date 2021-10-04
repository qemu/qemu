..
   Copyright (c) 2020, Linaro

Guest Loader
------------

The guest loader is similar to the ``generic-loader`` although it is
aimed at a particular use case of loading hypervisor guests. This is
useful for debugging hypervisors without having to jump through the
hoops of firmware and boot-loaders.

The guest loader does two things:

  - load blobs (kernels and initial ram disks) into memory
  - sets platform FDT data so hypervisors can find and boot them

This is what is typically done by a boot-loader like grub using it's
multi-boot capability. A typical example would look like:

.. parsed-literal::

  |qemu_system| -kernel ~/xen.git/xen/xen \
    -append "dom0_mem=1G,max:1G loglvl=all guest_loglvl=all" \
    -device guest-loader,addr=0x42000000,kernel=Image,bootargs="root=/dev/sda2 ro console=hvc0 earlyprintk=xen" \
    -device guest-loader,addr=0x47000000,initrd=rootfs.cpio

In the above example the Xen hypervisor is loaded by the -kernel
parameter and passed it's boot arguments via -append. The Dom0 guest
is loaded into the areas of memory. Each blob will get
``/chosen/module@<addr>`` entry in the FDT to indicate it's location and
size. Additional information can be passed with by using additional
arguments.

Currently the only supported machines which use FDT data to boot are
the ARM and RiscV ``virt`` machines.

Arguments
^^^^^^^^^

The full syntax of the guest-loader is::

  -device guest-loader,addr=<addr>[,kernel=<file>,[bootargs=<args>]][,initrd=<file>]

``addr=<addr>``
  This is mandatory and indicates the start address of the blob.

``kernel|initrd=<file>``
  Indicates the filename of the kernel or initrd blob. Both blobs will
  have the "multiboot,module" compatibility string as well as
  "multiboot,kernel" or "multiboot,ramdisk" as appropriate.

``bootargs=<args>``
  This is an optional field for kernel blobs which will pass command
  like via the ``/chosen/module@<addr>/bootargs`` node.
