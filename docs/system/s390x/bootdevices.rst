Boot devices on s390x
=====================

Booting with bootindex parameter
--------------------------------

For classical mainframe guests (i.e. LPAR or z/VM installations), you always
have to explicitly specify the disk where you want to boot from (or "IPL" from,
in s390x-speak -- IPL means "Initial Program Load"). In particular, there can
also be only one boot device according to the architecture specification, thus
specifying multiple boot devices is not possible (yet).

So for booting an s390x guest in QEMU, you should always mark the
device where you want to boot from with the ``bootindex`` property, for
example::

 qemu-system-s390x -drive if=none,id=dr1,file=guest.qcow2 \
                   -device virtio-blk,drive=dr1,bootindex=1

For booting from a CD-ROM ISO image (which needs to include El-Torito boot
information in order to be bootable), it is recommended to specify a ``scsi-cd``
device, for example like this::

 qemu-system-s390x -blockdev file,node-name=c1,filename=... \
                   -device virtio-scsi \
                   -device scsi-cd,drive=c1,bootindex=1

Note that you really have to use the ``bootindex`` property to select the
boot device. The old-fashioned ``-boot order=...`` command of QEMU (and
also ``-boot once=...``) is not supported on s390x.


Booting without bootindex parameter
-----------------------------------

The QEMU guest firmware (the so-called s390-ccw bios) has also some rudimentary
support for scanning through the available block devices. So in case you did
not specify a boot device with the ``bootindex`` property, there is still a
chance that it finds a bootable device on its own and starts a guest operating
system from it. However, this scanning algorithm is still very rough and may
be incomplete, so that it might fail to detect a bootable device in many cases.
It is really recommended to always specify the boot device with the
``bootindex`` property instead.

This also means that you should avoid the classical short-cut commands like
``-hda``, ``-cdrom`` or ``-drive if=virtio``, since it is not possible to
specify the ``bootindex`` with these commands. Note that the convenience
``-cdrom`` option even does not give you a real (virtio-scsi) CD-ROM device on
s390x. Due to technical limitations in the QEMU code base, you will get a
virtio-blk device with this parameter instead, which might not be the right
device type for installing a Linux distribution via ISO image. It is
recommended to specify a CD-ROM device via ``-device scsi-cd`` (as mentioned
above) instead.


Selecting kernels with the ``loadparm`` property
------------------------------------------------

The ``s390-ccw-virtio`` machine supports the so-called ``loadparm`` parameter
which can be used to select the kernel on the disk of the guest that the
s390-ccw bios should boot. When starting QEMU, it can be specified like this::

 qemu-system-s390x -machine s390-ccw-virtio,loadparm=<string>

The first way to use this parameter is to use the word ``PROMPT`` as the
``<string>`` here. In that case the s390-ccw bios will show a list of
installed kernels on the disk of the guest and ask the user to enter a number
to chose which kernel should be booted -- similar to what can be achieved by
specifying the ``-boot menu=on`` option when starting QEMU. Note that the menu
list will only show the names of the installed kernels when using a DASD-like
disk image with 4k byte sectors. On normal SCSI-style disks with 512-byte
sectors, there is not enough space for the zipl loader on the disk to store
the kernel names, so you only get a list without names here.

The second way to use this parameter is to use a number in the range from 0
to 31. The numbers that can be used here correspond to the numbers that are
shown when using the ``PROMPT`` option, and the s390-ccw bios will then try
to automatically boot the kernel that is associated with the given number.
Note that ``0`` can be used to boot the default entry.


Booting from a network device
-----------------------------

Beside the normal guest firmware (which is loaded from the file ``s390-ccw.img``
in the data directory of QEMU, or via the ``-bios`` option), QEMU ships with
a small TFTP network bootloader firmware for virtio-net-ccw devices, too. This
firmware is loaded from a file called ``s390-netboot.img`` in the QEMU data
directory. In case you want to load it from a different filename instead,
you can specify it via the ``-global s390-ipl.netboot_fw=filename``
command line option.

The ``bootindex`` property is especially important for booting via the network.
If you don't specify the ``bootindex`` property here, the network bootloader
firmware code won't get loaded into the guest memory so that the network boot
will fail. For a successful network boot, try something like this::

 qemu-system-s390x -netdev user,id=n1,tftp=...,bootfile=... \
                   -device virtio-net-ccw,netdev=n1,bootindex=1

The network bootloader firmware also has basic support for pxelinux.cfg-style
configuration files. See the `PXELINUX Configuration page
<https://wiki.syslinux.org/wiki/index.php?title=PXELINUX#Configuration>`__
for details how to set up the configuration file on your TFTP server.
The supported configuration file entries are ``DEFAULT``, ``LABEL``,
``KERNEL``, ``INITRD`` and ``APPEND`` (see the `Syslinux Config file syntax
<https://wiki.syslinux.org/wiki/index.php?title=Config>`__ for more
information).
