Protected Virtualization on s390x
=================================

The memory and most of the registers of Protected Virtual Machines
(PVMs) are encrypted or inaccessible to the hypervisor, effectively
prohibiting VM introspection when the VM is running. At rest, PVMs are
encrypted and can only be decrypted by the firmware, represented by an
entity called Ultravisor, of specific IBM Z machines.


Prerequisites
-------------

To run PVMs, a machine with the Protected Virtualization feature, as
indicated by the Ultravisor Call facility (stfle bit 158), is
required. The Ultravisor needs to be initialized at boot by setting
``prot_virt=1`` on the host's kernel command line.

Running PVMs requires using the KVM hypervisor.

If those requirements are met, the capability ``KVM_CAP_S390_PROTECTED``
will indicate that KVM can support PVMs on that LPAR.


Running a Protected Virtual Machine
-----------------------------------

To run a PVM you will need to select a CPU model which includes the
``Unpack facility`` (stfle bit 161 represented by the feature
``unpack``/``S390_FEAT_UNPACK``), and add these options to the command line::

    -object s390-pv-guest,id=pv0 \
    -machine confidential-guest-support=pv0

Adding these options will:

* Ensure the ``unpack`` facility is available
* Enable the IOMMU by default for all I/O devices
* Initialize the PV mechanism

Passthrough (vfio) devices are currently not supported.

Host huge page backings are not supported. However guests can use huge
pages as indicated by its facilities.


Boot Process
------------

A secure guest image can either be loaded from disk or supplied on the
QEMU command line. Booting from disk is done by the unmodified
s390-ccw BIOS. I.e., the bootmap is interpreted, multiple components
are read into memory and control is transferred to one of the
components (zipl stage3). Stage3 does some fixups and then transfers
control to some program residing in guest memory, which is normally
the OS kernel. The secure image has another component prepended
(stage3a) that uses the new diag308 subcodes 8 and 10 to trigger the
transition into secure mode.

Booting from the image supplied on the QEMU command line requires that
the file passed via -kernel has the same memory layout as would result
from the disk boot. This memory layout includes the encrypted
components (kernel, initrd, cmdline), the stage3a loader and
metadata. In case this boot method is used, the command line
options -initrd and -cmdline are ineffective. The preparation of a PVM
image is done via the ``genprotimg`` tool from the s390-tools
collection.
