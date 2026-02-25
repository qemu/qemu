AWS Nitro Enclaves
==================

`AWS Nitro Enclaves <https://aws.amazon.com/ec2/nitro/nitro-enclaves/>`_
are isolated compute environments that run alongside EC2 instances.
They are created by partitioning CPU and memory resources from a parent
instance and launching a signed Enclave Image Format (EIF) file inside
a confidential VM managed by the Nitro Hypervisor.

QEMU supports launching Nitro Enclaves on EC2 instances that have
enclave support enabled, using the ``nitro`` accelerator and the
``nitro`` machine type.

Prerequisites
-------------

* An EC2 instance with Nitro Enclaves enabled
* The ``nitro_enclaves`` kernel module loaded (provides ``/dev/nitro_enclaves``)
* CPU cores allocated to the Nitro Enclaves pool via ``nitro-enclaves-allocator``
* Huge pages allocated for Nitro Enclaves via ``nitro-enclaves-allocator``

Quick Start
-----------

Launch a Nitro Enclave from a pre-built EIF file::

    $ qemu-system-x86_64 -accel nitro,debug-mode=on -M nitro -nographic \
        -smp 2 -m 512M -kernel enclave.eif

Launch an enclave from individual kernel and initrd files::

    $ qemu-system-x86_64 -accel nitro,debug-mode=on -M nitro -nographic \
        -smp 2 -m 512M -kernel vmlinuz -initrd initrd.cpio \
        -append "console=ttyS0"

The same commands work with ``qemu-system-aarch64`` on Graviton based EC2
instances.

Accelerator
-----------

The ``nitro`` accelerator (``-accel nitro``) drives the
``/dev/nitro_enclaves`` device to create and manage a Nitro Enclave.
It handles:

* Creating the enclave VM slot
* Donating memory regions (must be huge page backed)
* Adding vCPUs (must be full physical cores)
* Starting the enclave
* Notifying vsock bus devices of the enclave CID

Accelerator options:

``debug-mode=on|off``
    Enable debug mode. When enabled, the Nitro Hypervisor exposes the
    enclave's serial console output via a vsock port that the machine
    model automatically connects to. In debug mode, PCR values are zero.
    Default is ``off``.

Machine
-------

The ``nitro`` machine (``-M nitro``) is a minimal, architecture-independent
machine that provides only what a Nitro Enclave needs:

* RAM (huge page backed via memfd)
* vCPUs (defaults to ``host`` CPU type)
* A Nitro vsock bus with:

  - A heartbeat device (vsock server on port 9000)
  - A serial console bridge (vsock client, debug mode only)

Communication to the Nitro Enclave is limited to virtio-vsock. The Enclave
is allocated a CID at launch at which it is reachable. A specific CID can
be requested with ``-accel nitro,enclave-cid=<N>`` (0 lets the hypervisor
choose). The assigned CID is readable from the vsock bridge device::

    (qemu) qom-get /machine/peripheral/nitro-vsock enclave-cid

EIF Image Format
^^^^^^^^^^^^^^^^

Nitro Enclaves boot from EIF (Enclave Image Format) files. When
``-kernel`` points to an EIF file (detected by the ``.eif`` magic
bytes), it is loaded directly into guest memory.

When ``-kernel`` points to a regular kernel image (e.g. a bzImage or
Image), the machine automatically assembles a minimal EIF on the fly
from ``-kernel``, ``-initrd``, and ``-append``. This allows standard
direct kernel boot without external EIF tooling.

CPU Requirements
^^^^^^^^^^^^^^^^

Nitro Enclaves require full physical CPU cores. On hyperthreaded
systems, this means ``-smp`` must be a multiple of the threads per
core (typically 2).

Nitro Enclaves can only consume cores that are donated to the Nitro Enclave
CPU pool. You can configure the CPU pool using the ``nitro-enclaves-allocator``
tool or manually by writing to the nitro_enclaves cpu pool parameter. To
allocate vCPUs 1, 2 and 3, you can call::

  $ echo 1,2,3 | sudo tee /sys/module/nitro_enclaves/parameters/ne_cpus

Beware that on x86-64 systems, hyperthread siblings are not consecutive
and must be added in pairs to the pool. Consult tools like ``lstopo``
or ``lscpu`` for details about your instance's CPU topology.

Memory Requirements
^^^^^^^^^^^^^^^^^^^

Enclave memory must be huge page backed. The machine automatically
creates a memfd memory backend with huge pages enabled. To make the
huge page allocation work, ensure that huge pages are reserved in
the system. To reserve 1 GiB of memory on a 4 KiB PAGE_SIZE system,
you can call::

    $ echo 512 | sudo tee /proc/sys/vm/nr_hugepages

Emulated Nitro Enclaves
-----------------------

In addition to the native Nitro Enclaves invocation, you can also use
the emulated nitro-enclave machine target (see :doc:`i386/nitro-enclave`)
which implements the x86 Nitro Enclave device model. While -M nitro
delegates virtual machine device emulation to the Nitro Hypervisor, -M
nitro-enclave implements all devices itself, which means it also works
on non-EC2 instances.

If you require NSM based attestation backed by valid AWS certificates,
you must use -M nitro. The -M nitro-enclave model does not provide
you with an AWS signed attestation document.
