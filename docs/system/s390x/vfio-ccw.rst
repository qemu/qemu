Subchannel passthrough via vfio-ccw
===================================

vfio-ccw (based upon the mediated vfio device infrastructure) allows to
make certain I/O subchannels and their devices available to a guest. The
host will not interact with those subchannels/devices any more.

Note that while vfio-ccw should work with most non-QDIO devices, only ECKD
DASDs have really been tested.

Example configuration
---------------------

Step 1: configure the host device
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As every mdev is identified by a uuid, the first step is to obtain one::

  [root@host ~]# uuidgen
  7e270a25-e163-4922-af60-757fc8ed48c6

Note: it is recommended to use the ``mdevctl`` tool for actually configuring
the host device.

To define the same device as configured below to be started
automatically, use

::

   [root@host ~]# driverctl -b css set-override 0.0.0313 vfio_ccw
   [root@host ~]# mdevctl define -u 7e270a25-e163-4922-af60-757fc8ed48c6 \
                  -p 0.0.0313 -t vfio_ccw-io -a

If using ``mdevctl`` is not possible or wanted, follow the manual procedure
below.

* Locate the subchannel for the device (in this example, ``0.0.2b09``)::

    [root@host ~]# lscss | grep 0.0.2b09 | awk '{print $2}'
    0.0.0313

* Unbind the subchannel (in this example, ``0.0.0313``) from the standard
  I/O subchannel driver and bind it to the vfio-ccw driver::

    [root@host ~]# echo 0.0.0313 > /sys/bus/css/devices/0.0.0313/driver/unbind
    [root@host ~]# echo 0.0.0313 > /sys/bus/css/drivers/vfio_ccw/bind

* Create the mediated device (identified by the uuid)::

    [root@host ~]# echo "7e270a25-e163-4922-af60-757fc8ed48c6" > \
    /sys/bus/css/devices/0.0.0313/mdev_supported_types/vfio_ccw-io/create

Step 2: configure QEMU
~~~~~~~~~~~~~~~~~~~~~~

* Reference the created mediated device and (optionally) pick a device id to
  be presented in the guest (here, ``fe.0.1234``, which will end up visible
  in the guest as ``0.0.1234``::

    -device vfio-ccw,devno=fe.0.1234,sysfsdev=\
    /sys/bus/mdev/devices/7e270a25-e163-4922-af60-757fc8ed48c6

* Start the guest. The device (here, ``0.0.1234``) should now be usable::

    [root@guest ~]# lscss -d 0.0.1234
    Device   Subchan.  DevType CU Type Use  PIM PAM POM  CHPID
    ----------------------------------------------------------------------
    0.0.1234 0.0.0007  3390/0e 3990/e9      f0  f0  ff   1a2a3a0a 00000000
    [root@guest ~]# chccwdev -e 0.0.1234
    Setting device 0.0.1234 online
    Done
    [root@guest ~]# dmesg -t
    (...)
    dasd-eckd 0.0.1234: A channel path to the device has become operational
    dasd-eckd 0.0.1234: New DASD 3390/0E (CU 3990/01) with 10017 cylinders, 15 heads, 224 sectors
    dasd-eckd 0.0.1234: DASD with 4 KB/block, 7212240 KB total size, 48 KB/track, compatible disk layout
    dasda:VOL1/  0X2B09: dasda1
