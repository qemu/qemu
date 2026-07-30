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
