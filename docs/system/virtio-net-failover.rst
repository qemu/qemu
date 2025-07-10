======================================
QEMU virtio-net standby (net_failover)
======================================

This document explains the setup and usage of virtio-net standby feature which
is used to create a net_failover pair of devices.

The general idea is that we have a pair of devices, a (vfio-)pci and a
virtio-net device. Before migration the vfio device is unplugged and data flows
through the virtio-net device, on the target side another vfio-pci device is
plugged in to take over the data-path. In the guest the net_failover kernel
module will pair net devices with the same MAC address.

The two devices are called primary and standby device. The fast hardware based
networking device is called the primary device and the virtio-net device is the
standby device.

Restrictions
------------

Currently only PCIe devices are allowed as primary devices, this restriction
can be lifted in the future with enhanced QEMU support. Also, only networking
devices are allowed as primary device. The user needs to ensure that primary
and standby devices are not plugged into the same PCIe slot.

Usecase
-------

Virtio-net standby allows easy migration while using a passed-through
fast networking device by falling back to a virtio-net device for the
duration of the migration. It is like a simple version of a bond, the
difference is that it requires no configuration in the guest. When a
guest is live-migrated to another host QEMU will unplug the primary
device via the PCIe based hotplug handler and traffic will go through
the virtio-net device. On the target system the primary device will be
automatically plugged back and the net_failover module registers it
again as the primary device.

Usage
-----

The primary device can be hotplugged or be part of the startup configuration

.. code-block:: shell

  -device virtio-net-pci,netdev=hostnet1,id=net1,mac=52:54:00:6f:55:cc,bus=root2,failover=on

With the parameter ``failover=on`` the VIRTIO_NET_F_STANDBY feature will be enabled.

.. code-block:: shell

  -device vfio-pci,host=5e:00.2,id=hostdev0,bus=root1,failover_pair_id=net1

``failover_pair_id`` references the id of the virtio-net standby device.
This is only for pairing the devices within QEMU. The guest kernel
module net_failover will match devices with identical MAC addresses.

Hotplug
-------

Both primary and standby device can be hotplugged via the QEMU
monitor. Note that if the virtio-net device is plugged first a warning
will be issued that it couldn't find the primary device.

Migration
---------

A new migration state wait-unplug was added for this feature. If
failover primary devices are present in the configuration, migration
will go into this state. It will wait until the device unplug is
completed in the guest and then move into active state. On the target
system the primary devices will be automatically hotplugged when the
feature bit was negotiated for the virtio-net standby device.
