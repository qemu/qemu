.. SPDX-License-Identifier: GPL-2.0-or-later

=============
UFS Emulation
=============

QEMU emulates a Universal Flash Storage (UFS) host controller and the
logical units behind it. The host controller follows the UFSHCI
register interface and reports UFS version 4.1 to the guest.

Two devices are involved:

``ufs``
  The host controller on the PCI bus. A ``sysbus-ufs`` variant exists
  for boards that place the controller on the system bus; it cannot be
  created with ``-device``.

``ufs-lu``
  A logical unit attached to a host controller through ``bus=``. Each
  logical unit is backed by a block device.

The well-known logical units (REPORT LUNS, UFS Device, Boot and RPMB)
are created by the host controller and need no configuration.

Adding a UFS Device
===================

The following adds a host controller with MCQ enabled and one logical
unit backed by ``ufs.img``:

.. code-block:: console

    -drive file=ufs.img,if=none,id=lu0,format=raw \
    -device ufs,id=ufs0,serial=0001,mcq=on \
    -device ufs-lu,drive=lu0,bus=ufs0,lun=0

Add more ``ufs-lu`` devices with different ``lun`` values to create
more logical units. On top of the architecture defconfig, a Linux
guest needs ``CONFIG_SCSI_UFSHCD``, ``CONFIG_SCSI_UFSHCD_PCI`` and
``CONFIG_BLK_DEV_SD``; each logical unit then appears as a SCSI disk.

Host Controller Properties
==========================

``serial``
  Serial number string. It is accepted but not reported to the guest
  yet; the serial number string descriptor reads back empty.

``nutrs`` (default 32, maximum 32)
  Number of UTP transfer request slots.

``nutmrs`` (default 8, maximum 8)
  Number of UTP task management request slots advertised to the guest.
  Task management requests are not processed yet (see Limitations).

``mcq`` (default off)
  Advertise Multi-Circular Queue (MCQ) support. When off, the guest
  uses the single doorbell transfer request list.

``mcq-maxq`` (default 2, maximum 31)
  Number of MCQ submission and completion queue pairs.

``wb-max-size``, ``wb-min-size`` (default 0x400 and 0x100)
  Maximum and minimum WriteBooster buffer size, in allocation units.
  ``wb-min-size`` must not exceed ``wb-max-size``.

Logical Unit Properties
=======================

``drive``
  Backing block device.

``lun`` (default 0)
  Logical unit number, 0 to 31. A host controller accepts up to 32
  logical units.

Limitations
===========

* UniPro and M-PHY are not modelled. Only the UIC commands needed for
  link startup, DME attribute access and hibernation are handled; DME
  reads return fixed values, and DME writes, power mode changes and
  hibernation do not change any link state.
* UTP task management requests are not processed, so a guest that
  tries to abort a command or reset a logical unit gets no completion.
* The Boot and RPMB well-known logical units only answer basic SCSI
  commands such as INQUIRY; boot and RPMB data access are not emulated.
* The device cannot be migrated.
