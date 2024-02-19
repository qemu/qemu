================================
LUKS volume with detached header
================================

Introduction
============

This document gives an overview of the design of LUKS volume with detached
header and how to use it.

Background
==========

The LUKS format has ability to store the header in a separate volume from
the payload. We could extend the LUKS driver in QEMU to support this use
case.

Normally a LUKS volume has a layout:

::

         +-----------------------------------------------+
         |         |                |                    |
 disk    | header  |  key material  |  disk payload data |
         |         |                |                    |
         +-----------------------------------------------+

With a detached LUKS header, you need 2 disks so getting:

::

         +--------------------------+
 disk1   |   header  | key material |
         +--------------------------+
         +---------------------+
 disk2   |  disk payload data  |
         +---------------------+

There are a variety of benefits to doing this:

 * Secrecy - the disk2 cannot be identified as containing LUKS
             volume since there's no header
 * Control - if access to the disk1 is restricted, then even
             if someone has access to disk2 they can't unlock
             it. Might be useful if you have disks on NFS but
             want to restrict which host can launch a VM
             instance from it, by dynamically providing access
             to the header to a designated host
 * Flexibility - your application data volume may be a given
                 size and it is inconvenient to resize it to
                 add encryption.You can store the LUKS header
                 separately and use the existing storage
                 volume for payload
 * Recovery - corruption of a bit in the header may make the
              entire payload inaccessible. It might be
              convenient to take backups of the header. If
              your primary disk header becomes corrupt, you
              can unlock the data still by pointing to the
              backup detached header

Architecture
============

Take the qcow2 encryption, for example. The architecture of the
LUKS volume with detached header is shown in the diagram below.

There are two children of the root node: a file and a header.
Data from the disk payload is stored in the file node. The
LUKS header and key material are located in the header node,
as previously mentioned.

::

                       +-----------------------------+
  Root node            |          foo[luks]          |
                       +-----------------------------+
                          |                       |
                     file |                header |
                          |                       |
               +---------------------+    +------------------+
  Child node   |payload-format[qcow2]|    |header-format[raw]|
               +---------------------+    +------------------+
                          |                       |
                     file |                 file  |
                          |                       |
               +----------------------+  +---------------------+
  Child node   |payload-protocol[file]|  |header-protocol[file]|
               +----------------------+  +---------------------+
                          |                       |
                          |                       |
                          |                       |
                     Host storage            Host storage

Usage
=====

Create a LUKS disk with a detached header using qemu-img
--------------------------------------------------------

Shell commandline::

  # qemu-img create --object secret,id=sec0,data=abc123 -f luks \
    -o cipher-alg=aes-256,cipher-mode=xts -o key-secret=sec0 \
    -o detached-header=true test-header.img
  # qemu-img create -f qcow2 test-payload.qcow2 200G
  # qemu-img info 'json:{"driver":"luks","file":{"filename": \
    "test-payload.img"},"header":{"filename":"test-header.img"}}'

Set up a VM's LUKS volume with a detached header
------------------------------------------------

Qemu commandline::

  # qemu-system-x86_64 ... \
    -object '{"qom-type":"secret","id":"libvirt-3-format-secret", \
    "data":"abc123"}' \
    -blockdev '{"driver":"file","filename":"/path/to/test-header.img", \
    "node-name":"libvirt-1-storage"}' \
    -blockdev '{"node-name":"libvirt-1-format","read-only":false, \
    "driver":"raw","file":"libvirt-1-storage"}' \
    -blockdev '{"driver":"file","filename":"/path/to/test-payload.qcow2", \
    "node-name":"libvirt-2-storage"}' \
    -blockdev '{"node-name":"libvirt-2-format","read-only":false, \
    "driver":"qcow2","file":"libvirt-2-storage"}' \
    -blockdev '{"node-name":"libvirt-3-format","driver":"luks", \
    "file":"libvirt-2-format","header":"libvirt-1-format","key-secret": \
    "libvirt-3-format-secret"}' \
    -device '{"driver":"virtio-blk-pci","bus":XXX,"addr":YYY,"drive": \
    "libvirt-3-format","id":"virtio-disk1"}'

Add LUKS volume to a VM with a detached header
----------------------------------------------

1. object-add the secret for decrypting the cipher stored in
   LUKS header above::

    # virsh qemu-monitor-command vm '{"execute":"object-add", \
      "arguments":{"qom-type":"secret", "id": \
      "libvirt-4-format-secret", "data":"abc123"}}'

2. block-add the protocol node for LUKS header::

    # virsh qemu-monitor-command vm '{"execute":"blockdev-add", \
      "arguments":{"node-name":"libvirt-1-storage", "driver":"file", \
      "filename": "/path/to/test-header.img" }}'

3. block-add the raw-drived node for LUKS header::

    # virsh qemu-monitor-command vm '{"execute":"blockdev-add", \
      "arguments":{"node-name":"libvirt-1-format", "driver":"raw", \
      "file":"libvirt-1-storage"}}'

4. block-add the protocol node for disk payload image::

    # virsh qemu-monitor-command vm '{"execute":"blockdev-add", \
      "arguments":{"node-name":"libvirt-2-storage", "driver":"file", \
      "filename":"/path/to/test-payload.qcow2"}}'

5. block-add the qcow2-drived format node for disk payload data::

    # virsh qemu-monitor-command vm '{"execute":"blockdev-add", \
      "arguments":{"node-name":"libvirt-2-format", "driver":"qcow2", \
      "file":"libvirt-2-storage"}}'

6. block-add the luks-drived format node to link the qcow2 disk
   with the LUKS header by specifying the field "header"::

    # virsh qemu-monitor-command vm '{"execute":"blockdev-add", \
      "arguments":{"node-name":"libvirt-3-format", "driver":"luks", \
      "file":"libvirt-2-format", "header":"libvirt-1-format", \
      "key-secret":"libvirt-2-format-secret"}}'

7. hot-plug the virtio-blk device finally::

    # virsh qemu-monitor-command vm '{"execute":"device_add", \
      "arguments": {"driver":"virtio-blk-pci", \
      "drive": "libvirt-3-format", "id":"virtio-disk2"}}

TODO
====

1. Support the shared detached LUKS header within the VM.
