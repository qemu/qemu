.. _canokey:

CanoKey QEMU
------------

CanoKey [1]_ is an open-source secure key with supports of

* U2F / FIDO2 with Ed25519 and HMAC-secret
* OpenPGP Card V3.4 with RSA4096, Ed25519 and more [2]_
* PIV (NIST SP 800-73-4)
* HOTP / TOTP
* NDEF

All these platform-independent features are in canokey-core [3]_.

For different platforms, CanoKey has different implementations,
including both hardware implementions and virtual cards:

* CanoKey STM32 [4]_
* CanoKey Pigeon [5]_
* (virt-card) CanoKey USB/IP
* (virt-card) CanoKey FunctionFS

In QEMU, yet another CanoKey virt-card is implemented.
CanoKey QEMU exposes itself as a USB device to the guest OS.

With the same software configuration as a hardware key,
the guest OS can use all the functionalities of a secure key as if
there was actually an hardware key plugged in.

CanoKey QEMU provides much convenience for debugging:

* libcanokey-qemu supports debugging output thus developers can
  inspect what happens inside a secure key
* CanoKey QEMU supports trace event thus event
* QEMU USB stack supports pcap thus USB packet between the guest
  and key can be captured and analysed

Then for developers:

* For developers on software with secure key support (e.g. FIDO2, OpenPGP),
  they can see what happens inside the secure key
* For secure key developers, USB packets between guest OS and CanoKey
  can be easily captured and analysed

Also since this is a virtual card, it can be easily used in CI for testing
on code coping with secure key.

Building
========

libcanokey-qemu is required to use CanoKey QEMU.

.. code-block:: shell

    git clone https://github.com/canokeys/canokey-qemu
    mkdir canokey-qemu/build
    pushd canokey-qemu/build

If you want to install libcanokey-qemu in a different place,
add ``-DCMAKE_INSTALL_PREFIX=/path/to/your/place`` to cmake below.

.. code-block:: shell

    cmake ..
    make
    make install # may need sudo
    popd

Then configuring and building:

.. code-block:: shell

    # depending on your env, lib/pkgconfig can be lib64/pkgconfig
    export PKG_CONFIG_PATH=/path/to/your/place/lib/pkgconfig:$PKG_CONFIG_PATH
    ./configure --enable-canokey && make

Using CanoKey QEMU
==================

CanoKey QEMU stores all its data on a file of the host specified by the argument
when invoking qemu.

.. parsed-literal::

    |qemu_system| -usb -device canokey,file=$HOME/.canokey-file

Note: you should keep this file carefully as it may contain your private key!

The first time when the file is used, it is created and initialized by CanoKey,
afterwards CanoKey QEMU would just read this file.

After the guest OS boots, you can check that there is a USB device.

For example, If the guest OS is an Linux machine. You may invoke lsusb
and find CanoKey QEMU there:

.. code-block:: shell

    $ lsusb
    Bus 001 Device 002: ID 20a0:42d4 Clay Logic CanoKey QEMU

You may setup the key as guided in [6]_. The console for the key is at [7]_.

Debugging
=========

CanoKey QEMU consists of two parts, ``libcanokey-qemu.so`` and ``canokey.c``,
the latter of which resides in QEMU. The former provides core functionality
of a secure key while the latter provides platform-dependent functions:
USB packet handling.

If you want to trace what happens inside the secure key, when compiling
libcanokey-qemu, you should add ``-DQEMU_DEBUG_OUTPUT=ON`` in cmake command
line:

.. code-block:: shell

    cmake .. -DQEMU_DEBUG_OUTPUT=ON

If you want to trace events happened in canokey.c, use

.. parsed-literal::

    |qemu_system| --trace "canokey_*" \\
        -usb -device canokey,file=$HOME/.canokey-file

If you want to capture USB packets between the guest and the host, you can:

.. parsed-literal::

    |qemu_system| -usb -device canokey,file=$HOME/.canokey-file,pcap=key.pcap

Limitations
===========

Currently libcanokey-qemu.so has dozens of global variables as it was originally
designed for embedded systems. Thus one qemu instance can not have
multiple CanoKey QEMU running, namely you can not

.. parsed-literal::

    |qemu_system| -usb -device canokey,file=$HOME/.canokey-file \\
         -device canokey,file=$HOME/.canokey-file2

Also, there is no lock on canokey-file, thus two CanoKey QEMU instance
can not read one canokey-file at the same time.

References
==========

.. [1] `<https://canokeys.org>`_
.. [2] `<https://docs.canokeys.org/userguide/openpgp/#supported-algorithm>`_
.. [3] `<https://github.com/canokeys/canokey-core>`_
.. [4] `<https://github.com/canokeys/canokey-stm32>`_
.. [5] `<https://github.com/canokeys/canokey-pigeon>`_
.. [6] `<https://docs.canokeys.org/>`_
.. [7] `<https://console.canokeys.org/>`_
