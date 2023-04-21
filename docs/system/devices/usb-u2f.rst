Universal Second Factor (U2F) USB Key Device
============================================

U2F is an open authentication standard that enables relying parties
exposed to the internet to offer a strong second factor option for end
user authentication.

The second factor is provided by a device implementing the U2F
protocol. In case of a USB U2F security key, it is a USB HID device
that implements the U2F protocol.

QEMU supports both pass-through of a host U2F key device to a VM,
and software emulation of a U2F key.

``u2f-passthru``
----------------

The ``u2f-passthru`` device allows you to connect a real hardware
U2F key on your host to a guest VM. All requests made from the guest
are passed through to the physical security key connected to the
host machine and vice versa.

In addition, the dedicated pass-through allows you to share a single
U2F security key with several guest VMs, which is not possible with a
simple host device assignment pass-through.

You can specify the host U2F key to use with the ``hidraw``
option, which takes the host path to a Linux ``/dev/hidrawN`` device:

.. parsed-literal::
   |qemu_system| -usb -device u2f-passthru,hidraw=/dev/hidraw0

If you don't specify the device, the ``u2f-passthru`` device will
autoscan to take the first U2F device it finds on the host (this
requires a working libudev):

.. parsed-literal::
   |qemu_system| -usb -device u2f-passthru

``u2f-emulated``
----------------

``u2f-emulated`` is a completely software emulated U2F device.
It uses `libu2f-emu <https://github.com/MattGorko/libu2f-emu>`__
for the U2F key emulation. libu2f-emu
provides a complete implementation of the U2F protocol device part for
all specified transports given by the FIDO Alliance.

To work, an emulated U2F device must have four elements:

 * ec x509 certificate
 * ec private key
 * counter (four bytes value)
 * 48 bytes of entropy (random bits)

To use this type of device, these have to be configured, and these
four elements must be passed one way or another.

Assuming that you have a working libu2f-emu installed on the host,
there are three possible ways to configure the ``u2f-emulated`` device:

 * ephemeral
 * setup directory
 * manual

Ephemeral is the simplest way to configure; it lets the device generate
all the elements it needs for a single use of the lifetime of the device.
It is the default if you do not pass any other options to the device.

.. parsed-literal::
   |qemu_system| -usb -device u2f-emulated

You can pass the device the path of a setup directory on the host
using the ``dir`` option; the directory must contain these four files:

 * ``certificate.pem``: ec x509 certificate
 * ``private-key.pem``: ec private key
 * ``counter``: counter value
 * ``entropy``: 48 bytes of entropy

.. parsed-literal::
   |qemu_system| -usb -device u2f-emulated,dir=$dir

You can also manually pass the device the paths to each of these files,
if you don't want them all to be in the same directory, using the options

 * ``cert``
 * ``priv``
 * ``counter``
 * ``entropy``

.. parsed-literal::
   |qemu_system| -usb -device u2f-emulated,cert=$DIR1/$FILE1,priv=$DIR2/$FILE2,counter=$DIR3/$FILE3,entropy=$DIR4/$FILE4
