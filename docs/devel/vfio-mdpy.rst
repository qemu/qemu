.. SPDX-License-Identifier: GPL-2.0-or-later

===================================
Testing VFIO display with mdev mdpy
===================================

.. contents:: Table of Contents

The kernel provides a sample mediated device driver, ``mdpy``
(``samples/vfio-mdev/mdpy.c``), that exposes a fake framebuffer through the VFIO
display region interface. It can be used to test VFIO display support, including
hotplug, without any real GPU hardware.

The kernel modules
==================

The ``mdpy`` driver depends on the ``mdev`` subsystem. Enable, build and load
the modules.

The minimal set is::

    CONFIG_SAMPLE_VFIO_MDEV_MDPY=m
    CONFIG_SAMPLE_VFIO_MDEV_MDPY_FB=m   # guest framebuffer driver

CONFIG_VFIO_MDEV is selected automatically.

Verify that the driver registered successfully:

.. code-block:: bash

    ls /sys/devices/virtual/mdpy/mdpy/mdev_supported_types/

Creating an mdev instance
=========================

Available types correspond to different resolutions (e.g. ``mdpy-vga``
for 640x480, ``mdpy-xga`` for 1024x768, ``mdpy-hd`` for 1920x1080).

Each mdev instance is identified by a UUID:

.. code-block:: bash

    uuid=$(uuidgen)
    echo "$uuid" > /sys/devices/virtual/mdpy/mdpy/mdev_supported_types/mdpy-xga/create

To remove the instance later:

.. code-block:: bash

    echo 1 > /sys/bus/mdev/devices/$uuid/remove

Make sure your user has the necessary permissions to access the vfio group.
(ex: chmod 666 /dev/vfio/16)

Starting QEMU
=============

Boot-time attachment
--------------------

.. code-block:: bash

    qemu-system-x86_64 -machine q35 -m 1G \
        -device vfio-pci,sysfsdev=/sys/bus/mdev/devices/$uuid,display=on \
        -display gtk,gl=on

Hotplug via HMP
---------------

Start QEMU with a PCIe root port (required for PCIe hotplug) and a
monitor:

.. code-block:: bash

    qemu-system-x86_64 -machine q35 -m 1G \
        -device pcie-root-port,id=rp0,slot=1 \
        -display gtk,gl=on \
        -monitor stdio

Then at the ``(qemu)`` prompt:

.. code-block:: none

    device_add vfio-pci,sysfsdev=/sys/bus/mdev/devices/<uuid>,display=on,bus=rp0,id=mdpy0

To hot-unplug:

.. code-block:: none

    device_del mdpy0
