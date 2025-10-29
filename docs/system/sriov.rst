.. SPDX-License-Identifier: GPL-2.0-or-later

Composable SR-IOV device
========================

SR-IOV (Single Root I/O Virtualization) is an optional extended capability of a
PCI Express device. It allows a single physical function (PF) to appear as
multiple virtual functions (VFs) for the main purpose of eliminating software
overhead in I/O from virtual machines.

There are devices with predefined SR-IOV configurations, but it is also possible
to compose an SR-IOV device yourself. Composing an SR-IOV device is currently
only supported by virtio-net-pci.

Users can configure an SR-IOV-capable virtio-net device by adding
virtio-net-pci functions to a bus. Below is a command line example:

.. code-block:: shell

    -netdev user,id=n -netdev user,id=o
    -netdev user,id=p -netdev user,id=q
    -device pcie-root-port,id=b
    -device virtio-net-pci,bus=b,addr=0x0.0x3,netdev=q,sriov-pf=f
    -device virtio-net-pci,bus=b,addr=0x0.0x2,netdev=p,sriov-pf=f
    -device virtio-net-pci,bus=b,addr=0x0.0x1,netdev=o,sriov-pf=f
    -device virtio-net-pci,bus=b,addr=0x0.0x0,netdev=n,id=f

The VFs specify the paired PF with ``sriov-pf`` property. The PF must be
added after all VFs. It is the user's responsibility to ensure that VFs have
function numbers larger than one of the PF, and that the function numbers
have a consistent stride. Both the PF and VFs are ARI-capable so you can have
255 VFs at maximum.

You may also need to perform additional steps to activate the SR-IOV feature on
your guest. For Linux, refer to [1]_.

.. [1] https://docs.kernel.org/PCI/pci-iov-howto.html
