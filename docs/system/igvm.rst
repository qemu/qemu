Independent Guest Virtual Machine (IGVM) support
================================================

IGVM files are designed to encapsulate all the information required to launch a
virtual machine on any given virtualization stack in a deterministic way. This
allows the cryptographic measurement of initial guest state for Confidential
Guests to be calculated when the IGVM file is built, allowing a relying party to
verify the initial state of a guest via a remote attestation.

Although IGVM files are designed with Confidential Computing in mind, they can
also be used to configure non-confidential guests. Multiple platforms can be
defined by a single IGVM file, allowing a single IGVM file to configure a
virtual machine that can run on, for example, TDX, SEV and non-confidential
hosts.

QEMU supports IGVM files through the user-creatable ``igvm-cfg`` object. This
object is used to define the filename of the IGVM file to process. A reference
to the object is added to the ``-machine`` to configure the virtual machine
to use the IGVM file for configuration.

Confidential platform support is provided through the use of
the ``ConfidentialGuestSupport`` object. If the virtual machine provides an
instance of this object then this is used by the IGVM loader to configure the
isolation properties of the directives within the file.

Further Information on IGVM
---------------------------

Information about the IGVM format, including links to the format specification
and documentation for the Rust and C libraries can be found at the project
repository:

https://github.com/microsoft/igvm


Supported Platforms
-------------------

Currently, IGVM files can be provided for Confidential Guests on host systems
that support AMD SEV, SEV-ES and SEV-SNP with KVM. IGVM files can also be
provided for non-confidential guests.


Limitations when using IGVM with AMD SEV, SEV-ES and SEV-SNP
------------------------------------------------------------

IGVM files configure the initial state of the guest using a set of directives.
Not every directive is supported by every Confidential Guest type. For example,
AMD SEV does not support encrypted save state regions, therefore setting the
initial CPU state using IGVM for SEV is not possible. When an IGVM file contains
directives that are not supported for the active platform, an error is generated
and the guest launch is aborted.

The table below describes the list of directives that are supported for SEV,
SEV-ES, SEV-SNP and non-confidential platforms.

.. list-table:: SEV, SEV-ES, SEV-SNP & non-confidential Supported Directives
   :widths: 35 65
   :header-rows: 1

   * - IGVM directive
     - Notes
   * - IGVM_VHT_PAGE_DATA
     - ``NORMAL`` zero, measured and unmeasured page types are supported. Other
       page types result in an error.
   * - IGVM_VHT_PARAMETER_AREA
     -
   * - IGVM_VHT_PARAMETER_INSERT
     -
   * - IGVM_VHT_VP_COUNT_PARAMETER
     - The guest parameter page is populated with the CPU count.
   * - IGVM_VHT_ENVIRONMENT_INFO_PARAMETER
     - The ``memory_is_shared`` parameter is set to 1 in the guest parameter
       page.

.. list-table:: Additional SEV, SEV-ES & SEV_SNP Supported Directives
   :widths: 25 75
   :header-rows: 1

   * - IGVM directive
     - Notes
   * - IGVM_VHT_MEMORY_MAP
     - The memory map page is populated using entries from the E820 table.
   * - IGVM_VHT_REQUIRED_MEMORY
     - Ensures memory is available in the guest at the specified range.

.. list-table:: Additional SEV-ES & SEV-SNP Supported Directives
   :widths: 25 75
   :header-rows: 1

   * - IGVM directive
     - Notes
   * - IGVM_VHT_VP_CONTEXT
     - Setting of the initial CPU state for the boot CPU and additional CPUs is
       supported with limitations on the fields that can be provided in the
       VMSA. See below for details on which fields are supported.

Initial CPU state with VMSA
---------------------------

The initial state of guest CPUs can be defined in the IGVM file for AMD SEV-ES
and SEV-SNP. The state data is provided as a VMSA structure as defined in Table
B-4 in the AMD64 Architecture Programmer's Manual, Volume 2 [1].

The IGVM VMSA is translated to CPU state in QEMU which is then synchronized
by KVM to the guest VMSA during the launch process where it contributes to the
launch measurement. See :ref:`amd-sev` for details on the launch process and
guest launch measurement.

It is important that no information is lost or changed when translating the
VMSA provided by the IGVM file into the VSMA that is used to launch the guest.
Therefore, QEMU restricts the VMSA fields that can be provided in the IGVM
VMSA structure to the following registers:

RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8-R15, RSP, RIP, CS, DS, ES, FS, GS, SS,
CR0, CR3, CR4, XCR0, EFER, PAT, GDT, IDT, LDTR, TR, DR6, DR7, RFLAGS, X87_FCW,
MXCSR.

When processing the IGVM file, QEMU will check if any fields other than the
above are non-zero and generate an error if this is the case.

KVM uses a hardcoded GPA of 0xFFFFFFFFF000 for the VMSA. When an IGVM file
defines initial CPU state, the GPA for each VMSA must match this hardcoded
value.

Firmware Images with IGVM
-------------------------

When an IGVM filename is specified for a Confidential Guest Support object it
overrides the default handling of system firmware: the firmware image, such as
an OVMF binary should be contained as a payload of the IGVM file and not
provided as a flash drive or via the ``-bios`` parameter. The default QEMU
firmware is not automatically populated into the guest memory space.

If an IGVM file is provided along with either the ``-bios`` parameter or pflash
devices then an error is displayed and the guest startup is aborted.

Running a guest configured using IGVM
-------------------------------------

To run a guest configured with IGVM you firstly need to generate an IGVM file
that contains a guest configuration compatible with the platform you are
targeting.

The ``buildigvm`` tool [2] is an example of a tool that can be used to generate
IGVM files for non-confidential X86 platforms as well as for SEV, SEV-ES and
SEV-SNP confidential platforms.

Example using this tool to generate an IGVM file for AMD SEV-SNP::

    buildigvm --firmware /path/to/OVMF.fd --output sev-snp.igvm \
              --cpucount 4 sev-snp

To run a guest configured with the generated IGVM you need to add an
``igvm-cfg`` object and refer to it from the ``-machine`` parameter:

Example (for AMD SEV)::

    qemu-system-x86_64 \
        <other parameters> \
        -machine ...,confidential-guest-support=sev0,igvm-cfg=igvm0 \
        -object sev-guest,id=sev0,cbitpos=47,reduced-phys-bits=1 \
        -object igvm-cfg,id=igvm0,file=/path/to/sev-snp.igvm

References
----------

[1] AMD64 Architecture Programmer's Manual, Volume 2: System Programming
  Rev 3.41
  https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24593.pdf

[2] ``buildigvm`` - A tool to build example IGVM files containing OVMF firmware
  https://github.com/roy-hopkins/buildigvm