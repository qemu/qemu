'nitro-enclave' virtual machine (``nitro-enclave``)
===================================================

``nitro-enclave`` is a machine type which emulates an *AWS nitro enclave*
virtual machine. `AWS nitro enclaves`_ is an Amazon EC2 feature that allows
creating isolated execution environments, called enclaves, from Amazon EC2
instances which are used for processing highly sensitive data. Enclaves have
no persistent storage and no external networking. The enclave VMs are based
on Firecracker microvm with a vhost-vsock device for communication with the
parent EC2 instance that spawned it and a Nitro Secure Module (NSM) device
for cryptographic attestation. The parent instance VM always has CID 3 while
the enclave VM gets a dynamic CID. Enclaves use an EIF (`Enclave Image Format`_)
file which contains the necessary kernel, cmdline and ramdisk(s) to boot.

In QEMU, ``nitro-enclave`` is a machine type based on ``microvm`` similar to how
AWS nitro enclaves look like a `Firecracker`_ microvm. This is useful for
local testing of EIF files using QEMU instead of running real AWS Nitro Enclaves
which can be difficult for debugging due to its roots in security. The vsock
device emulation is done using vhost-user-vsock which means another process that
can do the userspace emulation, like `vhost-device-vsock`_ from rust-vmm crate,
must be run alongside nitro-enclave for the vsock communication to work.

``libcbor`` and ``gnutls`` are required dependencies for nitro-enclave machine
support to be added when building QEMU from source.

.. _AWS nitro enclaves: https://docs.aws.amazon.com/enclaves/latest/user/nitro-enclave.html
.. _Enclave Image Format: https://github.com/aws/aws-nitro-enclaves-image-format
.. _vhost-device-vsock: https://github.com/rust-vmm/vhost-device/tree/main/vhost-device-vsock
.. _Firecracker: https://firecracker-microvm.github.io

Using the nitro-enclave machine type
------------------------------------

Machine-specific options
~~~~~~~~~~~~~~~~~~~~~~~~

It supports the following machine-specific options:

- nitro-enclave.vsock=string (required) (Id of the chardev from '-chardev' option that vhost-user-vsock device will use)
- nitro-enclave.id=string (optional) (Set enclave identifier)
- nitro-enclave.parent-role=string (optional) (Set parent instance IAM role ARN)
- nitro-enclave.parent-id=string (optional) (Set parent instance identifier)


Running a nitro-enclave VM
~~~~~~~~~~~~~~~~~~~~~~~~~~

First, run `vhost-device-vsock`__ (or a similar tool that supports vhost-user-vsock).
The forward-cid option below with value 1 forwards all connections from the enclave
VM to the host machine and the forward-listen (port numbers separated by '+') is used
for forwarding connections from the host machine to the enclave VM::

  $ vhost-device-vsock \
     --vm guest-cid=4,forward-cid=1,forward-listen=9001+9002,socket=/tmp/vhost4.socket

__ https://github.com/rust-vmm/vhost-device/tree/main/vhost-device-vsock#using-the-vsock-backend

Now run the necessary applications on the host machine so that the nitro-enclave VM
applications' vsock communication works. For example, the nitro-enclave VM's init
process connects to CID 3 and sends a single byte hello heartbeat (0xB7) to let the
parent VM know that it booted expecting a heartbeat (0xB7) response. So you must run
a AF_VSOCK server on the host machine that listens on port 9000 and sends the heartbeat
after it receives the heartbeat for enclave VM to boot successfully. You should run all
the applications on the host machine that would typically be running in the parent EC2
VM for successful communication with the enclave VM.

Then run the nitro-enclave VM using the following command where ``hello.eif`` is
an EIF file you would use to spawn a real AWS nitro enclave virtual machine::

  $ qemu-system-x86_64 -M nitro-enclave,vsock=c,id=hello-world \
     -kernel hello-world.eif -nographic -m 4G --enable-kvm -cpu host \
     -chardev socket,id=c,path=/tmp/vhost4.socket

In this example, the nitro-enclave VM has CID 4. If there are applications that
connect to the enclave VM, run them on the host machine after enclave VM starts.
You need to modify the applications to connect to CID 1 (instead of the enclave
VM's CID) and use the forward-listen (e.g., 9001+9002) option of vhost-device-vsock
to forward the ports they connect to.
