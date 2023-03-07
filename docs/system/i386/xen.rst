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
accelerator, for example for Xen 4.10:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x4000a,kernel-irqchip=split

Additionally, virtual APIC support can be advertised to the guest through the
``xen-vapic`` CPU flag:

.. parsed-literal::

  |qemu_system| --accel kvm,xen-version=0x4000a,kernel-irqchip=split --cpu host,+xen_vapic

When Xen support is enabled, QEMU changes hypervisor identification (CPUID
0x40000000..0x4000000A) to Xen. The KVM identification and features are not
advertised to a Xen guest. If Hyper-V is also enabled, the Xen identification
moves to leaves 0x40000100..0x4000010A.

The Xen platform device is enabled automatically for a Xen guest. This allows
a guest to unplug all emulated devices, in order to use Xen PV block and network
drivers instead. Under Xen, the boot disk is typically available both via IDE
emulation, and as a PV block device. Guest bootloaders typically use IDE to load
the guest kernel, which then unplugs the IDE and continues with the Xen PV block
device.

This configuration can be achieved as follows

.. parsed-literal::

  |qemu_system| -M pc --accel kvm,xen-version=0x4000a,kernel-irqchip=split \\
       -drive file=${GUEST_IMAGE},if=none,id=disk,file.locking=off -device xen-disk,drive=disk,vdev=xvda \\
       -drive file=${GUEST_IMAGE},index=2,media=disk,file.locking=off,if=ide

It is necessary to use the pc machine type, as the q35 machine uses AHCI instead
of legacy IDE, and AHCI disks are not unplugged through the Xen PV unplug
mechanism.

VirtIO devices can also be used; Linux guests may need to be dissuaded from
umplugging them by adding 'xen_emul_unplug=never' on their command line.

Properties
----------

The following properties exist on the KVM accelerator object:

``xen-version``
  This property contains the Xen version in ``XENVER_version`` form, with the
  major version in the top 16 bits and the minor version in the low 16 bits.
  Setting this property enables the Xen guest support.

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

OS requirements
---------------

The minimal Xen support in the KVM accelerator requires the host to be running
Linux v5.12 or newer. Later versions add optimisations: Linux v5.17 added
acceleration of interrupt delivery via the Xen PIRQ mechanism, and Linux v5.19
accelerated Xen PV timers and inter-processor interrupts (IPIs).
