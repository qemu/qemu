AMD Secure Encrypted Virtualization (SEV)
=========================================

Secure Encrypted Virtualization (SEV) is a feature found on AMD processors.

SEV is an extension to the AMD-V architecture which supports running encrypted
virtual machines (VMs) under the control of KVM. Encrypted VMs have their pages
(code and data) secured such that only the guest itself has access to the
unencrypted version. Each encrypted VM is associated with a unique encryption
key; if its data is accessed by a different entity using a different key the
encrypted guests data will be incorrectly decrypted, leading to unintelligible
data.

Key management for this feature is handled by a separate processor known as the
AMD secure processor (AMD-SP), which is present in AMD SOCs. Firmware running
inside the AMD-SP provides commands to support a common VM lifecycle. This
includes commands for launching, snapshotting, migrating and debugging the
encrypted guest. These SEV commands can be issued via KVM_MEMORY_ENCRYPT_OP
ioctls.

Secure Encrypted Virtualization - Encrypted State (SEV-ES) builds on the SEV
support to additionally protect the guest register state. In order to allow a
hypervisor to perform functions on behalf of a guest, there is architectural
support for notifying a guest's operating system when certain types of VMEXITs
are about to occur. This allows the guest to selectively share information with
the hypervisor to satisfy the requested function.

Launching
---------

Boot images (such as bios) must be encrypted before a guest can be booted. The
``MEMORY_ENCRYPT_OP`` ioctl provides commands to encrypt the images: ``LAUNCH_START``,
``LAUNCH_UPDATE_DATA``, ``LAUNCH_MEASURE`` and ``LAUNCH_FINISH``. These four commands
together generate a fresh memory encryption key for the VM, encrypt the boot
images and provide a measurement than can be used as an attestation of a
successful launch.

For a SEV-ES guest, the ``LAUNCH_UPDATE_VMSA`` command is also used to encrypt the
guest register state, or VM save area (VMSA), for all of the guest vCPUs.

``LAUNCH_START`` is called first to create a cryptographic launch context within
the firmware. To create this context, guest owner must provide a guest policy,
its public Diffie-Hellman key (PDH) and session parameters. These inputs
should be treated as a binary blob and must be passed as-is to the SEV firmware.

The guest policy is passed as plaintext. A hypervisor may choose to read it,
but should not modify it (any modification of the policy bits will result
in bad measurement). The guest policy is a 4-byte data structure containing
several flags that restricts what can be done on a running SEV guest.
See SEV API Spec ([SEVAPI]_) section 3 and 6.2 for more details.

The guest policy can be provided via the ``policy`` property::

  # ${QEMU} \
     sev-guest,id=sev0,policy=0x1...\

Setting the "SEV-ES required" policy bit (bit 2) will launch the guest as a
SEV-ES guest::

  # ${QEMU} \
     sev-guest,id=sev0,policy=0x5...\

The guest owner provided DH certificate and session parameters will be used to
establish a cryptographic session with the guest owner to negotiate keys used
for the attestation.

The DH certificate and session blob can be provided via the ``dh-cert-file`` and
``session-file`` properties::

  # ${QEMU} \
       sev-guest,id=sev0,dh-cert-file=<file1>,session-file=<file2>

``LAUNCH_UPDATE_DATA`` encrypts the memory region using the cryptographic context
created via the ``LAUNCH_START`` command. If required, this command can be called
multiple times to encrypt different memory regions. The command also calculates
the measurement of the memory contents as it encrypts.

``LAUNCH_UPDATE_VMSA`` encrypts all the vCPU VMSAs for a SEV-ES guest using the
cryptographic context created via the ``LAUNCH_START`` command. The command also
calculates the measurement of the VMSAs as it encrypts them.

``LAUNCH_MEASURE`` can be used to retrieve the measurement of encrypted memory and,
for a SEV-ES guest, encrypted VMSAs. This measurement is a signature of the
memory contents and, for a SEV-ES guest, the VMSA contents, that can be sent
to the guest owner as an attestation that the memory and VMSAs were encrypted
correctly by the firmware. The guest owner may wait to provide the guest
confidential information until it can verify the attestation measurement.
Since the guest owner knows the initial contents of the guest at boot, the
attestation measurement can be verified by comparing it to what the guest owner
expects.

``LAUNCH_FINISH`` finalizes the guest launch and destroys the cryptographic
context.

See SEV API Spec ([SEVAPI]_) 'Launching a guest' usage flow (Appendix A) for the
complete flow chart.

To launch a SEV guest::

  # ${QEMU} \
      -machine ...,confidential-guest-support=sev0 \
      -object sev-guest,id=sev0,cbitpos=47,reduced-phys-bits=1

To launch a SEV-ES guest::

  # ${QEMU} \
      -machine ...,confidential-guest-support=sev0 \
      -object sev-guest,id=sev0,cbitpos=47,reduced-phys-bits=1,policy=0x5

An SEV-ES guest has some restrictions as compared to a SEV guest. Because the
guest register state is encrypted and cannot be updated by the VMM/hypervisor,
a SEV-ES guest:

 - Does not support SMM - SMM support requires updating the guest register
   state.
 - Does not support reboot - a system reset requires updating the guest register
   state.
 - Requires in-kernel irqchip - the burden is placed on the hypervisor to
   manage booting APs.

Calculating expected guest launch measurement
---------------------------------------------

In order to verify the guest launch measurement, The Guest Owner must compute
it in the exact same way as it is calculated by the AMD-SP.  SEV API Spec
([SEVAPI]_) section 6.5.1 describes the AMD-SP operations:

    GCTX.LD is finalized, producing the hash digest of all plaintext data
    imported into the guest.

    The launch measurement is calculated as:

    HMAC(0x04 || API_MAJOR || API_MINOR || BUILD || GCTX.POLICY || GCTX.LD || MNONCE; GCTX.TIK)

    where "||" represents concatenation.

The values of API_MAJOR, API_MINOR, BUILD, and GCTX.POLICY can be obtained
from the ``query-sev`` qmp command.

The value of MNONCE is part of the response of ``query-sev-launch-measure``: it
is the last 16 bytes of the base64-decoded data field (see SEV API Spec
([SEVAPI]_) section 6.5.2 Table 52: LAUNCH_MEASURE Measurement Buffer).

The value of GCTX.LD is
``SHA256(firmware_blob || kernel_hashes_blob || vmsas_blob)``, where:

* ``firmware_blob`` is the content of the entire firmware flash file (for
  example, ``OVMF.fd``).  Note that you must build a stateless firmware file
  which doesn't use an NVRAM store, because the NVRAM area is not measured, and
  therefore it is not secure to use a firmware which uses state from an NVRAM
  store.
* if kernel is used, and ``kernel-hashes=on``, then ``kernel_hashes_blob`` is
  the content of PaddedSevHashTable (including the zero padding), which itself
  includes the hashes of kernel, initrd, and cmdline that are passed to the
  guest.  The PaddedSevHashTable struct is defined in ``target/i386/sev.c``.
* if SEV-ES is enabled (``policy & 0x4 != 0``), ``vmsas_blob`` is the
  concatenation of all VMSAs of the guest vcpus.  Each VMSA is 4096 bytes long;
  its content is defined inside Linux kernel code as ``struct vmcb_save_area``,
  or in AMD APM Volume 2 ([APMVOL2]_) Table B-2: VMCB Layout, State Save Area.

If kernel hashes are not used, or SEV-ES is disabled, use empty blobs for
``kernel_hashes_blob`` and ``vmsas_blob`` as needed.

Debugging
---------

Since the memory contents of a SEV guest are encrypted, hypervisor access to
the guest memory will return cipher text. If the guest policy allows debugging,
then a hypervisor can use the DEBUG_DECRYPT and DEBUG_ENCRYPT commands to access
the guest memory region for debug purposes.  This is not supported in QEMU yet.

Snapshot/Restore
----------------

TODO

Live Migration
---------------

TODO

References
----------

`AMD Memory Encryption whitepaper
<https://www.amd.com/content/dam/amd/en/documents/epyc-business-docs/white-papers/memory-encryption-white-paper.pdf>`_

.. [SEVAPI] `Secure Encrypted Virtualization API
   <https://www.amd.com/system/files/TechDocs/55766_SEV-KM_API_Specification.pdf>`_

.. [APMVOL2] `AMD64 Architecture Programmer's Manual Volume 2: System Programming
   <https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf>`_

KVM Forum slides:

* `AMD’s Virtualization Memory Encryption (2016)
  <http://www.linux-kvm.org/images/7/74/02x08A-Thomas_Lendacky-AMDs_Virtualizatoin_Memory_Encryption_Technology.pdf>`_
* `Extending Secure Encrypted Virtualization With SEV-ES (2018)
  <https://www.linux-kvm.org/images/9/94/Extending-Secure-Encrypted-Virtualization-with-SEV-ES-Thomas-Lendacky-AMD.pdf>`_

`AMD64 Architecture Programmer's Manual:
<https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf>`_

* SME is section 7.10
* SEV is section 15.34
* SEV-ES is section 15.35
