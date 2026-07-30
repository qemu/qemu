.. SPDX-License-Identifier: GPL-2.0-or-later

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


IPL Modes
---------

Multiple IPL modes are available to differentiate between the various IPL
configurations. These modes are mutually exclusive and enabled based on the
``boot-certs`` option on the QEMU command line.

Normal Mode
^^^^^^^^^^^

The absence of certificates will attempt to IPL a guest without secure IPL
operations. No checks are performed, and no warnings/errors are reported.
This is the default mode.

Configuration:

.. code-block:: shell

    qemu-system-s390x -machine s390-ccw-virtio ...
