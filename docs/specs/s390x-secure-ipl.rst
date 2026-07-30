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
