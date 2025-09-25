PowerNV family boards (``powernv8``, ``powernv9``, ``powernv10``, ``powernv11``)
================================================================================

PowerNV (as Non-Virtualized) is the "bare metal" platform using the
OPAL firmware. It runs Linux on IBM and OpenPOWER systems and it can
be used as an hypervisor OS, running KVM guests, or simply as a host
OS.

The PowerNV QEMU machine tries to emulate a PowerNV system at the
level of the skiboot firmware, which loads the OS and provides some
runtime services. Power Systems have a lower firmware (HostBoot) that
does low level system initialization, like DRAM training. This is
beyond the scope of what QEMU addresses today.

Supported devices
-----------------

 * Multi processor support for POWER8, POWER8NVL, POWER9, Power10 and Power11.
 * XSCOM, serial communication sideband bus to configure chiplets.
 * Simple LPC Controller.
 * Processor Service Interface (PSI) Controller.
 * Interrupt Controller, XICS (POWER8) and XIVE (POWER9) and XIVE2 (Power10 &
   Power11).
 * POWER8 PHB3 PCIe Host bridge and POWER9 PHB4 PCIe Host bridge.
 * Simple OCC is an on-chip micro-controller used for power management tasks.
 * iBT device to handle BMC communication, with the internal BMC simulator
   provided by QEMU or an external BMC such as an Aspeed QEMU machine.
 * PNOR containing the different firmware partitions.

Missing devices
---------------

A lot is missing, among which :

 * I2C controllers (yet to be merged).
 * NPU/NPU2/NPU3 controllers.
 * EEH support for PCIe Host bridge controllers.
 * NX controller.
 * VAS controller.
 * chipTOD (Time Of Day).
 * Self Boot Engine (SBE).
 * FSI bus.

Firmware
--------

The OPAL firmware (OpenPower Abstraction Layer) for OpenPower systems
includes the runtime services ``skiboot`` and the bootloader kernel and
initramfs ``skiroot``. Source code can be found on the `OpenPOWER account at
GitHub <https://github.com/open-power>`_.

Prebuilt images of ``skiboot`` and ``skiroot`` are made available on the
`OpenPOWER <https://github.com/open-power/op-build/releases/>`__ site.

QEMU includes a prebuilt image of ``skiboot`` which is updated when a
more recent version is required by the models.

Current acceleration status
---------------------------

KVM acceleration in Linux Power hosts is provided by the kvm-hv and
kvm-pr modules. kvm-hv is adherent to PAPR and it's not compliant with
powernv. kvm-pr in theory could be used as a valid accel option but
this isn't supported by kvm-pr at this moment.

To spare users from dealing with not so informative errors when attempting
to use accel=kvm, the powernv machine will throw an error informing that
KVM is not supported. This can be revisited in the future if kvm-pr (or
any other KVM alternative) is usable as KVM accel for this machine.

Boot options
------------

Here is a simple setup with one e1000e NIC :

.. code-block:: bash

  $ qemu-system-ppc64 -m 2G -machine powernv9 -smp 2,cores=2,threads=1 \
  -accel tcg,thread=single \
  -device e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=pcie.0,addr=0x0 \
  -netdev user,id=net0,hostfwd=::20022-:22,hostname=pnv \
  -kernel ./zImage.epapr  \
  -initrd ./rootfs.cpio.xz \
  -nographic

and a SATA disk :

.. code-block:: bash

  -device ich9-ahci,id=sata0,bus=pcie.1,addr=0x0 \
  -drive file=./ubuntu-ppc64le.qcow2,if=none,id=drive0,format=qcow2,cache=none \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0,id=ide,bootindex=1 \

Complex PCIe configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

Six PHBs are defined per chip (POWER9) but no default PCI layout is
provided (to be compatible with libvirt). One PCI device can be added
on any of the available PCIe slots using command line options such as:

.. code-block:: bash

  -device e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=pcie.0,addr=0x0
  -netdev bridge,id=net0,helper=/usr/libexec/qemu-bridge-helper,br=virbr0,id=hostnet0

  -device megasas,id=scsi0,bus=pcie.0,addr=0x0
  -drive file=./ubuntu-ppc64le.qcow2,if=none,id=drive-scsi0-0-0-0,format=qcow2,cache=none
  -device scsi-hd,bus=scsi0.0,channel=0,scsi-id=0,lun=0,drive=drive-scsi0-0-0-0,id=scsi0-0-0-0,bootindex=2

Here is a full example with two different storage controllers on
different PHBs, each with a disk, the second PHB is empty :

.. code-block:: bash

  $ qemu-system-ppc64 -m 2G -machine powernv9 -smp 2,cores=2,threads=1 -accel tcg,thread=single \
  -kernel ./zImage.epapr -initrd ./rootfs.cpio.xz -bios ./skiboot.lid \
  \
  -device megasas,id=scsi0,bus=pcie.0,addr=0x0 \
  -drive file=./rhel7-ppc64le.qcow2,if=none,id=drive-scsi0-0-0-0,format=qcow2,cache=none \
  -device scsi-hd,bus=scsi0.0,channel=0,scsi-id=0,lun=0,drive=drive-scsi0-0-0-0,id=scsi0-0-0-0,bootindex=2 \
  \
  -device pcie-pci-bridge,id=bridge1,bus=pcie.1,addr=0x0 \
  \
  -device ich9-ahci,id=sata0,bus=bridge1,addr=0x1 \
  -drive file=./ubuntu-ppc64le.qcow2,if=none,id=drive0,format=qcow2,cache=none \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0,id=ide,bootindex=1 \
  -device e1000e,netdev=net0,mac=C0:FF:EE:00:00:02,bus=bridge1,addr=0x2 \
  -netdev bridge,helper=/usr/libexec/qemu-bridge-helper,br=virbr0,id=net0 \
  -device nec-usb-xhci,bus=bridge1,addr=0x7 \
  \
  -serial mon:stdio -nographic

You can also use VIRTIO devices :

.. code-block:: bash

  -drive file=./fedora-ppc64le.qcow2,if=none,snapshot=on,id=drive0 \
  -device virtio-blk-pci,drive=drive0,id=blk0,bus=pcie.0 \
  \
  -netdev tap,helper=/usr/lib/qemu/qemu-bridge-helper,br=virbr0,id=netdev0 \
  -device virtio-net-pci,netdev=netdev0,id=net0,bus=pcie.1 \
  \
  -fsdev local,id=fsdev0,path=$HOME,security_model=passthrough \
  -device virtio-9p-pci,fsdev=fsdev0,mount_tag=host,bus=pcie.2

Multi sockets
~~~~~~~~~~~~~

The number of sockets is deduced from the number of CPUs and the
number of cores. ``-smp 2,cores=1`` will define a machine with 2
sockets of 1 core, whereas ``-smp 2,cores=2`` will define a machine
with 1 socket of 2 cores. ``-smp 8,cores=2``, 4 sockets of 2 cores.

BMC configuration
~~~~~~~~~~~~~~~~~

OpenPOWER systems negotiate the shutdown and reboot with their
BMC. The QEMU PowerNV machine embeds an IPMI BMC simulator using the
iBT interface and should offer the same power features.

If you want to define your own BMC, use ``-nodefaults`` and specify
one on the command line :

.. code-block:: bash

  -device ipmi-bmc-sim,id=bmc0 -device isa-ipmi-bt,bmc=bmc0,irq=10

The files `palmetto-SDR.bin <http://www.kaod.org/qemu/powernv/palmetto-SDR.bin>`__
and `palmetto-FRU.bin <http://www.kaod.org/qemu/powernv/palmetto-FRU.bin>`__
define a Sensor Data Record repository and a Field Replaceable Unit
inventory for a Palmetto BMC. They can be used to extend the QEMU BMC
simulator.

.. code-block:: bash

  -device ipmi-bmc-sim,sdrfile=./palmetto-SDR.bin,fruareasize=256,frudatafile=./palmetto-FRU.bin,id=bmc0 \
  -device isa-ipmi-bt,bmc=bmc0,irq=10

The PowerNV machine can also be run with an external IPMI BMC device
connected to a remote QEMU machine acting as BMC, using these options
:

.. code-block:: bash

  -chardev socket,id=ipmi0,host=localhost,port=9002,reconnect-ms=10000 \
  -device ipmi-bmc-extern,id=bmc0,chardev=ipmi0 \
  -device isa-ipmi-bt,bmc=bmc0,irq=10 \
  -nodefaults

NVRAM
~~~~~

Use a MTD drive to add a PNOR to the machine, and get a NVRAM :

.. code-block:: bash

  -drive file=./witherspoon.pnor,format=raw,if=mtd

If no mtd drive is provided, the powernv platform will create a default
PNOR device using a tiny formatted PNOR in pc-bios/pnv-pnor.bin opened
read-only (PNOR changes will be persistent across reboots but not across
invocations of QEMU). If no defaults are used, an erased 128MB PNOR is
provided (which skiboot will probably not recognize since it is not
formatted).

Maintainer contact information
------------------------------

Cédric Le Goater <clg@kaod.org>
