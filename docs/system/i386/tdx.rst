Intel Trusted Domain eXtension (TDX)
====================================

Intel Trusted Domain eXtensions (TDX) refers to an Intel technology that extends
Virtual Machine Extensions (VMX) and Multi-Key Total Memory Encryption (MKTME)
with a new kind of virtual machine guest called a Trust Domain (TD). A TD runs
in a CPU mode that is designed to protect the confidentiality of its memory
contents and its CPU state from any other software, including the hosting
Virtual Machine Monitor (VMM), unless explicitly shared by the TD itself.

Prerequisites
-------------

To run TD, the physical machine needs to have TDX module loaded and initialized
while KVM hypervisor has TDX support and has TDX enabled. If those requirements
are met, the ``KVM_CAP_VM_TYPES`` will report the support of ``KVM_X86_TDX_VM``.

Trust Domain Virtual Firmware (TDVF)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Trust Domain Virtual Firmware (TDVF) is required to provide TD services to boot
TD Guest OS. TDVF needs to be copied to guest private memory and measured before
the TD boots.

KVM vcpu ioctl ``KVM_TDX_INIT_MEM_REGION`` can be used to populate the TDVF
content into its private memory.

Since TDX doesn't support readonly memslot, TDVF cannot be mapped as pflash
device and it actually works as RAM. "-bios" option is chosen to load TDVF.

OVMF is the opensource firmware that implements the TDVF support. Thus the
command line to specify and load TDVF is ``-bios OVMF.fd``

Feature Configuration
---------------------

Unlike non-TDX VM, the CPU features (enumerated by CPU or MSR) of a TD are not
under full control of VMM. VMM can only configure part of features of a TD on
``KVM_TDX_INIT_VM`` command of VM scope ``MEMORY_ENCRYPT_OP`` ioctl.

The configurable features have three types:

- Attributes:
  - PKS (bit 30) controls whether Supervisor Protection Keys is exposed to TD,
  which determines related CPUID bit and CR4 bit;
  - PERFMON (bit 63) controls whether PMU is exposed to TD.

- XSAVE related features (XFAM):
  XFAM is a 64b mask, which has the same format as XCR0 or IA32_XSS MSR. It
  determines the set of extended features available for use by the guest TD.

- CPUID features:
  Only some bits of some CPUID leaves are directly configurable by VMM.

What features can be configured is reported via TDX capabilities.

TDX capabilities
~~~~~~~~~~~~~~~~

The VM scope ``MEMORY_ENCRYPT_OP`` ioctl provides command ``KVM_TDX_CAPABILITIES``
to get the TDX capabilities from KVM. It returns a data structure of
``struct kvm_tdx_capabilities``, which tells the supported configuration of
attributes, XFAM and CPUIDs.

TD attributes
~~~~~~~~~~~~~

QEMU supports configuring raw 64-bit TD attributes directly via "attributes"
property of "tdx-guest" object. Note, it's users' responsibility to provide a
valid value because some bits may not supported by current QEMU or KVM yet.

QEMU also supports the configuration of individual attribute bits that are
supported by it, via properties of "tdx-guest" object.
E.g., "sept-ve-disable" (bit 28).

MSR based features
~~~~~~~~~~~~~~~~~~

Current KVM doesn't support MSR based feature (e.g., MSR_IA32_ARCH_CAPABILITIES)
configuration for TDX, and it's a future work to enable it in QEMU when KVM adds
support of it.

Feature check
~~~~~~~~~~~~~

QEMU checks if the final (CPU) features, determined by given cpu model and
explicit feature adjustment of "+featureA/-featureB", can be supported or not.
It can produce feature not supported warning like

  "warning: host doesn't support requested feature: CPUID.07H:EBX.intel-pt [bit 25]"

It can also produce warning like

  "warning: TDX forcibly sets the feature: CPUID.80000007H:EDX.invtsc [bit 8]"

if the fixed-1 feature is requested to be disabled explicitly. This is newly
added to QEMU for TDX because TDX has fixed-1 features that are forcibly enabled
by TDX module and VMM cannot disable them.

Launching a TD (TDX VM)
-----------------------

To launch a TD, the necessary command line options are tdx-guest object and
split kernel-irqchip, as below:

.. parsed-literal::

    |qemu_system_x86| \\
        -accel kvm \\
        -cpu host \\
        -object tdx-guest,id=tdx0 \\
        -machine ...,confidential-guest-support=tdx0 \\
        -bios OVMF.fd \\

Restrictions
------------

 - kernel-irqchip must be split;

   This is set by default for TDX guest if kernel-irqchip is left on its default
   'auto' setting.

 - No readonly support for private memory;

 - No SMM support: SMM support requires manipulating the guest register states
   which is not allowed;

Debugging
---------

Bit 0 of TD attributes, is DEBUG bit, which decides if the TD runs in off-TD
debug mode. When in off-TD debug mode, TD's VCPU state and private memory are
accessible via given SEAMCALLs. This requires KVM to expose APIs to invoke those
SEAMCALLs and corresonponding QEMU change.

It's targeted as future work.

TD attestation
--------------

In TD guest, the attestation process is used to verify the TDX guest
trustworthiness to other entities before provisioning secrets to the guest.

TD attestation is initiated first by calling TDG.MR.REPORT inside TD to get the
REPORT. Then the REPORT data needs to be converted into a remotely verifiable
Quote by SGX Quoting Enclave (QE).

It's a future work in QEMU to add support of TD attestation since it lacks
support in current KVM.

Live Migration
--------------

Future work.

References
----------

- `TDX Homepage <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-trust-domain-extensions.html>`__

- `SGX QE <https://github.com/intel/SGXDataCenterAttestationPrimitives/tree/master/QuoteGeneration>`__
