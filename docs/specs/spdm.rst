======================================================
QEMU Security Protocols and Data Models (SPDM) Support
======================================================

SPDM enables authentication, attestation and key exchange to assist in
providing infrastructure security enablement. It's a standard published
by the `DMTF`_.

QEMU supports connecting to a SPDM responder implementation. This allows an
external application to emulate the SPDM responder logic for an SPDM device.

Setting up a SPDM server
========================

When using QEMU with SPDM devices QEMU will connect to a server which
implements the SPDM functionality.

SPDM-Utils
----------

You can use `SPDM Utils`_ to emulate a responder. This is the simplest method.

SPDM-Utils is a Linux applications to manage, test and develop devices
supporting DMTF Security Protocol and Data Model (SPDM). It is written in Rust
and utilises libspdm.

To use SPDM-Utils you will need to do the following steps. Details are included
in the SPDM-Utils README.

 1. `Build libspdm`_
 2. `Build SPDM Utils`_
 3. `Run it as a server`_

spdm-emu
--------

You can use `spdm emu`_ to model the
SPDM responder.

.. code-block:: shell

    $ cd spdm-emu
    $ git submodule init; git submodule update --recursive
    $ mkdir build; cd build
    $ cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Debug -DCRYPTO=openssl ..
    $ make -j32
    $ make copy_sample_key # Build certificates, required for SPDM authentication.

It is worth noting that the certificates should be in compliance with
PCIe r6.1 sec 6.31.3. This means you will need to add the following to
openssl.cnf

.. code-block::

    subjectAltName = otherName:2.23.147;UTF8:Vendor=1b36:Device=0010:CC=010802:REV=02:SSVID=1af4:SSID=1100
    2.23.147 = ASN1:OID:2.23.147

and then manually regenerate some certificates with:

.. code-block:: shell

    $ openssl req -nodes -newkey ec:param.pem -keyout end_responder.key \
        -out end_responder.req -sha384 -batch \
        -subj "/CN=DMTF libspdm ECP384 responder cert"

    $ openssl x509 -req -in end_responder.req -out end_responder.cert \
        -CA inter.cert -CAkey inter.key -sha384 -days 3650 -set_serial 3 \
        -extensions v3_end -extfile ../openssl.cnf

    $ openssl asn1parse -in end_responder.cert -out end_responder.cert.der

    $ cat ca.cert.der inter.cert.der end_responder.cert.der > bundle_responder.certchain.der

You can use SPDM-Utils instead as it will generate the correct certificates
automatically.

The responder can then be launched with

.. code-block:: shell

    $ cd bin
    $ ./spdm_responder_emu --trans PCI_DOE

Connecting an SPDM NVMe device
==============================

Once a SPDM server is running we can start QEMU and connect to the server.

For an NVMe device first let's setup a block we can use

.. code-block:: shell

    $ cd qemu-spdm/linux/image
    $ dd if=/dev/zero of=blknvme bs=1M count=2096 # 2GB NNMe Drive

Then you can add this to your QEMU command line:

.. code-block:: shell

    -drive file=blknvme,if=none,id=mynvme,format=raw \
        -device nvme,drive=mynvme,serial=deadbeef,spdm_port=2323,spdm_trans=doe

At which point QEMU will try to connect to the SPDM server.

Note that if using x86_64 you will want to use the q35 machine instead
of the default. So the entire QEMU command might look like this

.. code-block:: shell

    qemu-system-x86_64 -M q35 \
        --kernel bzImage \
        -drive file=rootfs.ext2,if=virtio,format=raw \
        -append "root=/dev/vda console=ttyS0" \
        -net none -nographic \
        -drive file=blknvme,if=none,id=mynvme,format=raw \
        -device nvme,drive=mynvme,serial=deadbeef,spdm_port=2323,spdm_trans=doe

The ``spdm_trans`` argument defines the underlying transport type that is
emulated by QEMU. For an PCIe NVMe controller, both "doe" and "nvme" are
supported. Where, "doe" does SPDM transport over the PCIe extended capability
Data Object Exchange (DOE), and "nvme" uses the NVMe Admin Security
Send/Receive commands to implement the SPDM transport.

.. _DMTF:
   https://www.dmtf.org/standards/SPDM

.. _SPDM Utils:
   https://github.com/westerndigitalcorporation/spdm-utils

.. _spdm emu:
   https://github.com/dmtf/spdm-emu

.. _Build libspdm:
   https://github.com/westerndigitalcorporation/spdm-utils?tab=readme-ov-file#build-libspdm

.. _Build SPDM Utils:
   https://github.com/westerndigitalcorporation/spdm-utils?tab=readme-ov-file#build-the-binary

.. _Run it as a server:
   https://github.com/westerndigitalcorporation/spdm-utils#qemu-spdm-device-emulation
