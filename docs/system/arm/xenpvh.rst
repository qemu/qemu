XENPVH (``xenpvh``)
=========================================
This machine creates a IOREQ server to register/connect with Xen Hypervisor.

When TPM is enabled, this machine also creates a tpm-tis-device at a user input
tpm base address, adds a TPM emulator and connects to a swtpm application
running on host machine via chardev socket. This enables xenpvh to support TPM
functionalities for a guest domain.

More information about TPM use and installing swtpm linux application can be
found at: docs/specs/tpm.rst.

Example for starting swtpm on host machine:
.. code-block:: console

    mkdir /tmp/vtpm2
    swtpm socket --tpmstate dir=/tmp/vtpm2 \
    --ctrl type=unixio,path=/tmp/vtpm2/swtpm-sock &

Sample QEMU xenpvh commands for running and connecting with Xen:
.. code-block:: console

    qemu-system-aarch64 -xen-domid 1 \
    -chardev socket,id=libxl-cmd,path=qmp-libxl-1,server=on,wait=off \
    -mon chardev=libxl-cmd,mode=control \
    -chardev socket,id=libxenstat-cmd,path=qmp-libxenstat-1,server=on,wait=off \
    -mon chardev=libxenstat-cmd,mode=control \
    -xen-attach -name guest0 -vnc none -display none -nographic \
    -machine xenpvh -m 1301 \
    -chardev socket,id=chrtpm,path=tmp/vtpm2/swtpm-sock \
    -tpmdev emulator,id=tpm0,chardev=chrtpm -machine tpm-base-addr=0x0C000000

In above QEMU command, last two lines are for connecting xenpvh QEMU to swtpm
via chardev socket.
