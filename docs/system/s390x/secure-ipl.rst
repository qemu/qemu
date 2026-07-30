.. SPDX-License-Identifier: GPL-2.0-or-later

s390 Secure IPL
===============

Secure IPL, also known as secure boot, enables s390-ccw virtual machines to
verify the integrity of guest kernels.

For technical details of this feature, see the
:doc:`specs document </specs/s390x-secure-ipl>`.

This document explains how to use secure IPL with s390x in QEMU. It covers
the command line options for providing certificates and enabling secure IPL,
the different IPL modes (Normal, Audit, and Secure), and system requirements.

A quickstart guide is provided to demonstrate how to generate certificates,
sign images, and start a guest in Secure Mode.


Secure IPL Command Line Options
-------------------------------

The s390-ccw-virtio machine type supports secure IPL. These parameters allow
users to provide certificates and enable secure IPL directly via the command
line.

Providing Certificates
^^^^^^^^^^^^^^^^^^^^^^

The certificate store can be populated by supplying a list of X.509 certificate
file paths or directories containing certificate files on the command-line:

Note: certificate files must have a .pem extension.

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,boot-certs.0.path=/.../qemu/certs,boot-certs.1.path=/another/path/cert.pem ...

Enabling Secure IPL
^^^^^^^^^^^^^^^^^^^

Secure IPL is enabled by explicitly setting ``secure-boot=on``; if not
specified, secure boot is considered off.

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on|off


IPL Modes
---------

Multiple IPL modes are available to differentiate between the various IPL
configurations. These modes are mutually exclusive and enabled based on specific
combinations of the ``secure-boot`` and ``boot-certs`` options on the QEMU
command line.

Normal Mode
^^^^^^^^^^^

The absence of both certificates and the ``secure-boot`` option will attempt to
IPL a guest without secure IPL operations. No checks are performed, and no
warnings/errors are reported.  This is the default mode, and can be explicitly
enabled with ``secure-boot=off``.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio ...

Audit Mode
^^^^^^^^^^

When the certificate store is populated with at least one certificate
and no additional secure IPL parameters are provided on the command
line, then secure IPL will proceed in "audit mode". All secure IPL
operations will be performed with signature verification errors reported
as non-disruptive warnings.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,boot-certs.0.path=/.../qemu/certs,boot-certs.1.path=/another/path/cert.pem ...

Secure Mode
^^^^^^^^^^^

When the ``secure-boot=on`` option is set and certificates are provided,
a secure boot is performed with error reporting enabled. The boot process aborts
if any error occurs.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on,boot-certs.0.path=/.../qemu/certs,boot-certs.1.path=/another/path/cert.pem ...


Constraints
-----------

The following constraints apply when attempting to boot an s390x guest in secure
mode:

- z16 or "qemu" CPU model
- certificates must be in X.509 PEM format
- only support for SCSI scheme of virtio-blk/virtio-scsi devices
- a boot device must be specified
- any unsupported devices (e.g., ECKD and VFIO) or non-eligible devices (e.g.,
  network) will cause the entire boot process to terminate early, with an error
  logged to the console.


Secure IPL Quickstart
---------------------

Build QEMU with gnutls enabled
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: shell

    ./configure … --enable-gnutls

Generate certificate (e.g. via certtool)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A private key is required before generating a certificate. This key must be kept
secure and confidential.

Use an RSA private key for signing.

.. code-block:: shell

    certtool --generate-privkey > key.pem

A self-signed certificate requires the organization name. Use the ``cert.info``
template to pre-fill values and avoid interactive prompts from certtool.

.. code-block:: shell

    cat > cert.info <<EOF
    cn = "My Name"
    expiration_days = 365
    cert_signing_key
    EOF

    certtool --generate-self-signed \
             --load-privkey key.pem \
             --template cert.info \
             --hash=SHA256 \
             --outfile cert.pem

Sign Images (e.g. via sign-file)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- signing must be performed on a guest filesystem
- sign-file script used in the example below is located within the kernel source
  repo

.. code-block:: shell

    ./sign-file sha256 key.pem cert.pem /boot/vmlinuz-…
    ./sign-file sha256 key.pem cert.pem /usr/lib/s390-tools/stage3.bin

Note: re-signing a component will not verify correctly; the existing signature
must be stripped before a new one is applied.

Run zipl with secure boot enabled
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- zipl must be performed on a guest filesystem

.. code-block:: shell

    zipl --secure 1 -V

Command line options for starting the guest
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio,secure-boot=on,boot-certs.0.path=cert.pem ...
