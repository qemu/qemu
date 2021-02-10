Multi-process QEMU
==================

This document describes how to configure and use multi-process qemu.
For the design document refer to docs/devel/qemu-multiprocess.

1) Configuration
----------------

multi-process is enabled by default for targets that enable KVM


2) Usage
--------

Multi-process QEMU requires an orchestrator to launch.

Following is a description of command-line used to launch mpqemu.

* Orchestrator:

  - The Orchestrator creates a unix socketpair

  - It launches the remote process and passes one of the
    sockets to it via command-line.

  - It then launches QEMU and specifies the other socket as an option
    to the Proxy device object

* Remote Process:

  - QEMU can enter remote process mode by using the "remote" machine
    option.

  - The orchestrator creates a "remote-object" with details about
    the device and the file descriptor for the device

  - The remaining options are no different from how one launches QEMU with
    devices.

  - Example command-line for the remote process is as follows:

      /usr/bin/qemu-system-x86_64                                        \
      -machine x-remote                                                  \
      -device lsi53c895a,id=lsi0                                         \
      -drive id=drive_image2,file=/build/ol7-nvme-test-1.qcow2           \
      -device scsi-hd,id=drive2,drive=drive_image2,bus=lsi0.0,scsi-id=0  \
      -object x-remote-object,id=robj1,devid=lsi1,fd=4,

* QEMU:

  - Since parts of the RAM are shared between QEMU & remote process, a
    memory-backend-memfd is required to facilitate this, as follows:

    -object memory-backend-memfd,id=mem,size=2G

  - A "x-pci-proxy-dev" device is created for each of the PCI devices emulated
    in the remote process. A "socket" sub-option specifies the other end of
    unix channel created by orchestrator. The "id" sub-option must be specified
    and should be the same as the "id" specified for the remote PCI device

  - Example commandline for QEMU is as follows:

      -device x-pci-proxy-dev,id=lsi0,socket=3
