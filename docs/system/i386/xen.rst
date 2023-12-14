Xen HVM guest support
=====================


Description
-----------

KVM has support for hosting Xen guests, intercepting Xen hypercalls and event
channel (Xen PV interrupt) delivery. This allows guests which expect to be
run under Xen to be hosted in QEMU under Linux/KVM instead.

Using the split irqchip is mandatory for Xen support.

Setup
-----

Xen mode is enabled by setting the ``xen-version`` property of the KVM
accelerator, for example for Xen 4.17:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x40011,kernel-irqchip=split

Additionally, virtual APIC support can be advertised to the guest through the
``xen-vapic`` CPU flag:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x40011,kernel-irqchip=split --cpu host,+xen-vapic

When Xen support is enabled, QEMU changes hypervisor identification (CPUID
0x40000000..0x4000000A) to Xen. The KVM identification and features are not
advertised to a Xen guest. If Hyper-V is also enabled, the Xen identification
moves to leaves 0x40000100..0x4000010A.

Properties
----------

The following properties exist on the KVM accelerator object:

``xen-version``
  This property contains the Xen version in ``XENVER_version`` form, with the
  major version in the top 16 bits and the minor version in the low 16 bits.
  Setting this property enables the Xen guest support. If Xen version 4.5 or
  greater is specified, the HVM leaf in Xen CPUID is populated. Xen version
  4.6 enables the vCPU ID in CPUID, and version 4.17 advertises vCPU upcall
  vector support to the guest.

``xen-evtchn-max-pirq``
  Xen PIRQs represent an emulated physical interrupt, either GSI or MSI, which
  can be routed to an event channel instead of to the emulated I/O or local
  APIC. By default, QEMU permits only 256 PIRQs because this allows maximum
  compatibility with 32-bit MSI where the higher bits of the PIRQ# would need
  to be in the upper 64 bits of the MSI message. For guests with large numbers
  of PCI devices (and none which are limited to 32-bit addressing) it may be
  desirable to increase this value.

``xen-gnttab-max-frames``
  Xen grant tables are the means by which a Xen guest grants access to its
  memory for PV back ends (disk, network, etc.). Since QEMU only supports v1
  grant tables which are 8 bytes in size, each page (each frame) of the grant
  table can reference 512 pages of guest memory. The default number of frames
  is 64, allowing for 32768 pages of guest memory to be accessed by PV backends
  through simultaneous grants. For guests with large numbers of PV devices and
  high throughput, it may be desirable to increase this value.

Xen paravirtual devices
-----------------------

The Xen PCI platform device is enabled automatically for a Xen guest. This
allows a guest to unplug all emulated devices, in order to use paravirtual
block and network drivers instead.

Those paravirtual Xen block, network (and console) devices can be created
through the command line, and/or hot-plugged.

To provide a Xen console device, define a character device and then a device
of type ``xen-console`` to connect to it. For the Xen console equivalent of
the handy ``-serial mon:stdio`` option, for example:

.. parsed-literal::
   -chardev stdio,mux=on,id=char0,signal=off -mon char0 \\
   -device xen-console,chardev=char0

The Xen network device is ``xen-net-device``, which becomes the default NIC
model for emulated Xen guests, meaning that just the default NIC provided
by QEMU should automatically work and present a Xen network device to the
guest.

Disks can be configured with '``-drive file=${GUEST_IMAGE},if=xen``' and will
appear to the guest as ``xvda`` onwards.

Under Xen, the boot disk is typically available both via IDE emulation, and
as a PV block device. Guest bootloaders typically use IDE to load the guest
kernel, which then unplugs the IDE and continues with the Xen PV block device.

This configuration can be achieved as follows:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x40011,kernel-irqchip=split \\
       -drive file=${GUEST_IMAGE},if=xen \\
       -drive file=${GUEST_IMAGE},file.locking=off,if=ide

VirtIO devices can also be used; Linux guests may need to be dissuaded from
umplugging them by adding '``xen_emul_unplug=never``' on their command line.

Booting Xen PV guests
---------------------

Booting PV guest kernels is possible by using the Xen PV shim (a version of Xen
itself, designed to run inside a Xen HVM guest and provide memory management
services for one guest alone).

The Xen binary is provided as the ``-kernel`` and the guest kernel itself (or
PV Grub image) as the ``-initrd`` image, which actually just means the first
multiboot "module". For example:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x40011,kernel-irqchip=split \\
       -chardev stdio,id=char0 -device xen-console,chardev=char0 \\
       -display none  -m 1G  -kernel xen -initrd bzImage \\
       -append "pv-shim console=xen,pv -- console=hvc0 root=/dev/xvda1" \\
       -drive file=${GUEST_IMAGE},if=xen

The Xen image must be built with the ``CONFIG_XEN_GUEST`` and ``CONFIG_PV_SHIM``
options, and as of Xen 4.17, Xen's PV shim mode does not support using a serial
port; it must have a Xen console or it will panic.

The example above provides the guest kernel command line after a separator
(" ``--`` ") on the Xen command line, and does not provide the guest kernel
with an actual initramfs, which would need to listed as a second multiboot
module. For more complicated alternatives, see the command line
documentation for the ``-initrd`` option.

Host OS requirements
--------------------

The minimal Xen support in the KVM accelerator requires the host to be running
Linux v5.12 or newer. Later versions add optimisations: Linux v5.17 added
acceleration of interrupt delivery via the Xen PIRQ mechanism, and Linux v5.19
accelerated Xen PV timers and inter-processor interrupts (IPIs).
