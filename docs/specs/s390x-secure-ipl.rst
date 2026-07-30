.. SPDX-License-Identifier: GPL-2.0-or-later

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
