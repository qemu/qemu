QEMU VM templating
==================

This document explains how to use VM templating in QEMU.

For now, the focus is on VM memory aspects, and not about how to save and
restore other VM state (i.e., migrate-to-file with ``x-ignore-shared``).

Overview
--------

With VM templating, a single template VM serves as the starting point for
new VMs. This allows for fast and efficient replication of VMs, resulting
in fast startup times and reduced memory consumption.

Conceptually, the VM state is frozen, to then be used as a basis for new
VMs. The Copy-On-Write mechanism in the operating systems makes sure that
new VMs are able to read template VM memory; however, any modifications
stay private and don't modify the original template VM or any other
created VM.

!!! Security Alert !!!
----------------------

When effectively cloning VMs by VM templating, hardware identifiers
(such as UUIDs and NIC MAC addresses), and similar data in the guest OS
(such as machine IDs, SSH keys, certificates) that are supposed to be
*unique* are no longer unique, which can be a security concern.

Please be aware of these implications and how to mitigate them for your
use case, which might involve vmgenid, hot(un)plug of NIC, etc..

Memory configuration
--------------------

In order to create the template VM, we have to make sure that VM memory
ends up in a file, from where it can be reused for the new VMs:

Supply VM RAM via memory-backend-file, with ``share=on`` (modifications go
to the file) and ``readonly=off`` (open the file writable). Note that
``readonly=off`` is implicit.

In the following command-line example, a 2GB VM is created, whereby VM RAM
is to be stored in the ``template`` file.

.. parsed-literal::

    |qemu_system| [...] -m 2g \\
        -object memory-backend-file,id=pc.ram,mem-path=template,size=2g,share=on,... \\
        -machine q35,memory-backend=pc.ram

If multiple memory backends are used (vNUMA, DIMMs), configure all
memory backends accordingly.

Once the VM is in the desired state, stop the VM and save other VM state,
leaving the current state of VM RAM reside in the file.

In order to have a new VM be based on a template VM, we have to
configure VM RAM to be based on a template VM RAM file; however, the VM
should not be able to modify file content.

Supply VM RAM via memory-backend-file, with ``share=off`` (modifications
stay private), ``readonly=on`` (open the file readonly) and ``rom=off``
(don't make the memory readonly for the VM). Note that ``share=off`` is
implicit and that other VM state has to be restored separately.

In the following command-line example, a 2GB VM is created based on the
existing 2GB file ``template``.

.. parsed-literal::

    |qemu_system| [...] -m 2g \\
        -object memory-backend-file,id=pc.ram,mem-path=template,size=2g,readonly=on,rom=off,... \\
        -machine q35,memory-backend=pc.ram

If multiple memory backends are used (vNUMA, DIMMs), configure all
memory backends accordingly.

Note that ``-mem-path`` cannot be used for VM templating when creating the
template VM or when starting new VMs based on a template VM.

Incompatible features
---------------------

Some features are incompatible with VM templating, as the underlying file
cannot be modified to discard VM RAM, or to actually share memory with
another process.

vhost-user and multi-process QEMU
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

vhost-user and multi-process QEMU are incompatible with VM templating.
These technologies rely on shared memory, however, the template VMs
don't actually share memory (``share=off``), even though they are
file-based.

virtio-balloon
~~~~~~~~~~~~~~

virtio-balloon inflation and "free page reporting" cannot discard VM RAM
and will repeatedly report errors. While virtio-balloon can be used
for template VMs (e.g., report VM RAM stats), "free page reporting"
should be disabled and the balloon should not be inflated.

virtio-mem
~~~~~~~~~~

virtio-mem cannot discard VM RAM that is managed by the virtio-mem
device. virtio-mem will fail early when realizing the device. To use
VM templating with virtio-mem, either hotplug virtio-mem devices to the
new VM, or don't supply any memory to the template VM using virtio-mem
(requested-size=0), not using a template VM file as memory backend for the
virtio-mem device.

VM migration
~~~~~~~~~~~~

For VM migration, "x-release-ram" similarly relies on discarding of VM
RAM on the migration source to free up migrated RAM, and will
repeatedly report errors.

Postcopy live migration fails discarding VM RAM on the migration
destination early and refuses to activate postcopy live migration. Note
that postcopy live migration usually only works on selected filesystems
(shmem/tmpfs, hugetlbfs) either way.
