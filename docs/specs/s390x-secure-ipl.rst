.. SPDX-License-Identifier: GPL-2.0-or-later

s390 Secure IPL
===============

Secure IPL (a.k.a. secure boot) enables s390-ccw virtual machines to
leverage qcrypto libraries and z/Architecture emulation to verify the
integrity of signed kernels. The qcrypto libraries are used to perform
certificate validation and signature-verification, whereas the
z/Architecture emulation is used to ensure secure IPL data has not
been tampered with, convey data between QEMU and guest code, and set up
the relevant secure IPL data structures with verification results.

To find out more about using this feature, see
:doc:`documentation </system/s390x/secure-ipl>`.

Note that "guest code" will refer to the s390-ccw BIOS unless stated
otherwise.

Both QEMU and guest code work in cooperation to perform secure IPL. The Secure
Code Loading Attributes Facility (SCLAF) is used to check the Secure Code
Loading Attribute Block (SCLAB) and ensure that secure IPL data has not
been tampered with. DIAGNOSE 'X'320' is invoked by guest code to query
the certificate store info and retrieve specific certificates from QEMU.
DIAGNOSE 'X'508' is used by guest code to leverage qcrypto libraries to
perform signature-verification in QEMU. Lastly, guest code generates and
appends an IPL Information Report Block (IIRB) at the end of the IPL
Parameter Block (IPLB), which is used by the kernel to store signed and
verified entries.

The logical steps are as follows:

- guest code reads data payload from disk (e.g. stage3 boot loader, kernel)
- guest code checks the validity of the SCLAB
- guest code invokes DIAG 508 subcode 1 and provides the payload
- QEMU handles DIAG 508 request by reading the payload and retrieving the
  certificate store
- QEMU DIAG 508 utilizes handler qcrypto libraries to perform
  signature-verification on the payload, attempting with each cert in the store
  (until success or exhausted)
- QEMU DIAG 508 returns:

  - success: index of cert used to verify payload
  - failure: error code

- guest code is expected to respond to this operation by:

  - success: retrieves cert from store via DIAG 320 using returned index
  - failure: reports with warning (audit mode), aborts with error (secure mode)

- guest code appends IIRB at the end of the IPLB
- guest code kicks off IPL

More information regarding the respective DIAGNOSE commands and IPL data
structures are outlined within this document.


s390 Certificate Store and Functions
------------------------------------

s390 Certificate Store
^^^^^^^^^^^^^^^^^^^^^^

A certificate store is implemented for s390-ccw guests to retain within
memory all certificates provided by the user via the command-line, which
are expected to be stored somewhere on the host's file system. The store
will keep track of the number of certificates, their respective size,
and a summation of the sizes.

Each certificate is stored in an S390IPLCertificate struct, which has a
name (converted to EBCDIC), size fields of PEM and DER data, and the raw
PEM Base64 data.

Note: A maximum of 64 certificates are allowed to be stored in the certificate
store.

DIAGNOSE function code 'X'320' - Certificate Store Facility
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

DIAGNOSE 'X'320' is used to provide support for guest code to directly
query the s390 certificate store. Guest code may be the s390-ccw BIOS or
the guest kernel.

Subcode 0 - query installed subcodes
    Returns a 256-bit installed subcodes mask (ISM) stored in the installed
    subcodes block (ISB). This mask indicates which subcodes are currently
    installed and available for use.

Subcode 1 - query verification certificate storage information
    Provides the information required to determine the amount of memory needed
    to store one or more verification-certificates (VCs) from the certificate
    store (CS).

    Upon successful completion, this subcode returns various storage size values
    for verification-certificate blocks (VCBs).

    The output is returned in the verification-certificate-storage-size block
    (VCSSB). A VCSSB length of 4 indicates that no certificates are available
    in the CS.

Subcode 2 - store verification certificates
    Provides VCs that are in the certificate store.

    The output is provided in a VCB, which includes a common header followed by
    zero or more verification-certificate entries (VCEs).

    The instruction expects the cert store to maintain an origin of 1 for the
    index (i.e. a retrieval of the first certificate in the store should be
    denoted by setting first-VC to 1).

    The first-VC and last-VC fields of the VCB specify the index range of
    VCs to be stored in the VCB. Certs are stored sequentially, starting
    with first-VC index. As each cert is stored, a "stored count" is
    incremented. If there is not enough space to store all certs requested
    by the index range, a "remaining count" will be recorded and no more
    certificates will be stored.

    Each VCE contains a header followed by information extracted from a
    certificate within the certificate store. The information includes:
    key-id, hash, and certificate data. This information is stored
    contiguously in a VCE (with zero-padding). Following the header, the
    key-id is immediately stored. The hash and certificate data follow and
    may be accessed via the respective offset fields stored in the VCE.


Secure IPL Data Structures, Facilities, and Functions
-----------------------------------------------------

DIAGNOSE function code 'X'508' - IPL extensions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

DIAGNOSE 'X'508' is reserved for guest use in order to facilitate communication
of additional IPL operations that cannot be handled by guest code, such as
signature verification for secure IPL.

If the function code specifies 0x508, IPL extension functions are performed.
These functions are meant to provide extended functionality for s390 guest boot
that requires assistance from QEMU.

Subcode 0 - query installed subcodes
    Returns a 64-bit mask indicating which subcodes are supported.

Subcode 1 - perform signature verification
    Perform signature-verification on a signed component, using certificates
    from the certificate store and leveraging qcrypto libraries to perform
    this operation.

    Note: verification of initrd is not supported.

    A return code of 1 indicates success, and the index and length of the
    corresponding certificate will be set in the Diag508SigVerifBlock.
    The following values indicate failure:

    * ``0x0102``: no certificates are available in the store
    * ``0x0202``: component data is invalid
    * ``0x0302``: PKCS#7 format signature is invalid
    * ``0x0402``: signature-verification failed
    * ``0x0502``: length of Diag508SigVerifBlock is invalid

IPL Information Report Block
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The IPL Parameter Block (IPLB), utilized for IPL operation, is extended with an
IPL Information Report Block (IIRB), which contains the results from secure IPL
operations such as:

* component data
* verification results
* certificate data

During early boot, the guest kernel reserves the memory region
containing the IIRB. This preserves the data while the guest kernel is
operating and during re-IPL.

The guest kernel uses the contents in the IIRB for:

* Boot logging: reports which components were loaded and verified.
* kexec operations: builds the next kernel’s IPL report from the existing one.
* Keying: installs IPL certificates into the platform trusted keyring.

Secure Code Loading Attributes Facility
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Secure Code Loading Attributes Facility (SCLAF) enhances system security
during the IPL by enforcing additional verification rules.

When SCLAF is available, its behavior depends on the IPL mode. It introduces
verification of both signed and unsigned components to help ensure that only
authorized code is loaded during the IPL process. Any errors detected by SCLAF
are reported in the IIRB.

Unsigned components are restricted to load addresses at or above absolute
storage address ``0x2000``.

Signed components must include a Secure Code Loading Attribute Block (SCLAB),
which is appended at the very end of the component. The SCLAB defines security
attributes for handling the signed code.
