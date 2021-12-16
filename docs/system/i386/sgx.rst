Software Guard eXtensions (SGX)
===============================

Overview
--------

Intel Software Guard eXtensions (SGX) is a set of instructions and mechanisms
for memory accesses in order to provide security accesses for sensitive
applications and data. SGX allows an application to use it's pariticular
address space as an *enclave*, which is a protected area provides confidentiality
and integrity even in the presence of privileged malware. Accesses to the
enclave memory area from any software not resident in the enclave are prevented,
including those from privileged software.

Virtual SGX
-----------

SGX feature is exposed to guest via SGX CPUID. Looking at SGX CPUID, we can
report the same CPUID info to guest as on host for most of SGX CPUID. With
reporting the same CPUID guest is able to use full capacity of SGX, and KVM
doesn't need to emulate those info.

The guest's EPC base and size are determined by QEMU, and KVM needs QEMU to
notify such info to it before it can initialize SGX for guest.

Virtual EPC
~~~~~~~~~~~

By default, QEMU does not assign EPC to a VM, i.e. fully enabling SGX in a VM
requires explicit allocation of EPC to the VM. Similar to other specialized
memory types, e.g. hugetlbfs, EPC is exposed as a memory backend.

SGX EPC is enumerated through CPUID, i.e. EPC "devices" need to be realized
prior to realizing the vCPUs themselves, which occurs long before generic
devices are parsed and realized.  This limitation means that EPC does not
require -maxmem as EPC is not treated as {cold,hot}plugged memory.

QEMU does not artificially restrict the number of EPC sections exposed to a
guest, e.g. QEMU will happily allow you to create 64 1M EPC sections. Be aware
that some kernels may not recognize all EPC sections, e.g. the Linux SGX driver
is hardwired to support only 8 EPC sections.

The following QEMU snippet creates two EPC sections, with 64M pre-allocated
to the VM and an additional 28M mapped but not allocated::

 -object memory-backend-epc,id=mem1,size=64M,prealloc=on \
 -object memory-backend-epc,id=mem2,size=28M \
 -M sgx-epc.0.memdev=mem1,sgx-epc.1.memdev=mem2

Note:

The size and location of the virtual EPC are far less restricted compared
to physical EPC. Because physical EPC is protected via range registers,
the size of the physical EPC must be a power of two (though software sees
a subset of the full EPC, e.g. 92M or 128M) and the EPC must be naturally
aligned.  KVM SGX's virtual EPC is purely a software construct and only
requires the size and location to be page aligned. QEMU enforces the EPC
size is a multiple of 4k and will ensure the base of the EPC is 4k aligned.
To simplify the implementation, EPC is always located above 4g in the guest
physical address space.

Migration
~~~~~~~~~

QEMU/KVM doesn't prevent live migrating SGX VMs, although from hardware's
perspective, SGX doesn't support live migration, since both EPC and the SGX
key hierarchy are bound to the physical platform. However live migration
can be supported in the sense if guest software stack can support recreating
enclaves when it suffers sudden lose of EPC; and if guest enclaves can detect
SGX keys being changed, and handle gracefully. For instance, when ERESUME fails
with #PF.SGX, guest software can gracefully detect it and recreate enclaves;
and when enclave fails to unseal sensitive information from outside, it can
detect such error and sensitive information can be provisioned to it again.

CPUID
~~~~~

Due to its myriad dependencies, SGX is currently not listed as supported
in any of QEMU's built-in CPU configuration. To expose SGX (and SGX Launch
Control) to a guest, you must either use ``-cpu host`` to pass-through the
host CPU model, or explicitly enable SGX when using a built-in CPU model,
e.g. via ``-cpu <model>,+sgx`` or ``-cpu <model>,+sgx,+sgxlc``.

All SGX sub-features enumerated through CPUID, e.g. SGX2, MISCSELECT,
ATTRIBUTES, etc... can be restricted via CPUID flags. Be aware that enforcing
restriction of MISCSELECT, ATTRIBUTES and XFRM requires intercepting ECREATE,
i.e. may marginally reduce SGX performance in the guest. All SGX sub-features
controlled via -cpu are prefixed with "sgx", e.g.::

  $ qemu-system-x86_64 -cpu help | xargs printf "%s\n" | grep sgx
  sgx
  sgx-debug
  sgx-encls-c
  sgx-enclv
  sgx-exinfo
  sgx-kss
  sgx-mode64
  sgx-provisionkey
  sgx-tokenkey
  sgx1
  sgx2
  sgxlc

The following QEMU snippet passes through the host CPU but restricts access to
the provision and EINIT token keys::

 -cpu host,-sgx-provisionkey,-sgx-tokenkey

SGX sub-features cannot be emulated, i.e. sub-features that are not present
in hardware cannot be forced on via '-cpu'.

Virtualize SGX Launch Control
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

QEMU SGX support for Launch Control (LC) is passive, in the sense that it
does not actively change the LC configuration.  QEMU SGX provides the user
the ability to set/clear the CPUID flag (and by extension the associated
IA32_FEATURE_CONTROL MSR bit in fw_cfg) and saves/restores the LE Hash MSRs
when getting/putting guest state, but QEMU does not add new controls to
directly modify the LC configuration.  Similar to hardware behavior, locking
the LC configuration to a non-Intel value is left to guest firmware.  Unlike
host bios setting for SGX launch control(LC), there is no special bios setting
for SGX guest by our design. If host is in locked mode, we can still allow
creating VM with SGX.

Feature Control
~~~~~~~~~~~~~~~

QEMU SGX updates the ``etc/msr_feature_control`` fw_cfg entry to set the SGX
(bit 18) and SGX LC (bit 17) flags based on their respective CPUID support,
i.e. existing guest firmware will automatically set SGX and SGX LC accordingly,
assuming said firmware supports fw_cfg.msr_feature_control.

Launching a guest
-----------------

To launch a SGX guest:

.. parsed-literal::

  |qemu_system_x86| \\
   -cpu host,+sgx-provisionkey \\
   -object memory-backend-epc,id=mem1,size=64M,prealloc=on \\
   -M sgx-epc.0.memdev=mem1,sgx-epc.0.node=0

Utilizing SGX in the guest requires a kernel/OS with SGX support.
The support can be determined in guest by::

  $ grep sgx /proc/cpuinfo

and SGX epc info by::

  $ dmesg | grep sgx
  [    0.182807] sgx: EPC section 0x140000000-0x143ffffff
  [    0.183695] sgx: [Firmware Bug]: Unable to map EPC section to online node. Fallback to the NUMA node 0.

To launch a SGX numa guest:

.. parsed-literal::

  |qemu_system_x86| \\
   -cpu host,+sgx-provisionkey \\
   -object memory-backend-ram,size=2G,host-nodes=0,policy=bind,id=node0 \\
   -object memory-backend-epc,id=mem0,size=64M,prealloc=on,host-nodes=0,policy=bind \\
   -numa node,nodeid=0,cpus=0-1,memdev=node0 \\
   -object memory-backend-ram,size=2G,host-nodes=1,policy=bind,id=node1 \\
   -object memory-backend-epc,id=mem1,size=28M,prealloc=on,host-nodes=1,policy=bind \\
   -numa node,nodeid=1,cpus=2-3,memdev=node1 \\
   -M sgx-epc.0.memdev=mem0,sgx-epc.0.node=0,sgx-epc.1.memdev=mem1,sgx-epc.1.node=1

and SGX epc numa info by::

  $ dmesg | grep sgx
  [    0.369937] sgx: EPC section 0x180000000-0x183ffffff
  [    0.370259] sgx: EPC section 0x184000000-0x185bfffff

  $ dmesg | grep SRAT
  [    0.009981] ACPI: SRAT: Node 0 PXM 0 [mem 0x180000000-0x183ffffff]
  [    0.009982] ACPI: SRAT: Node 1 PXM 1 [mem 0x184000000-0x185bfffff]

References
----------

- `SGX Homepage <https://software.intel.com/sgx>`__

- `SGX SDK <https://github.com/intel/linux-sgx.git>`__

- SGX specification: Intel SDM Volume 3
