.. _Network_Emulation:

Network emulation
-----------------

QEMU can simulate several network cards (e.g. PCI or ISA cards on the PC
target) and can connect them to a network backend on the host or an
emulated hub. The various host network backends can either be used to
connect the NIC of the guest to a real network (e.g. by using a TAP
devices or the non-privileged user mode network stack), or to other
guest instances running in another QEMU process (e.g. by using the
socket host network backend).

Using TAP network interfaces
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is the standard way to connect QEMU to a real network. QEMU adds a
virtual network device on your host (called ``tapN``), and you can then
configure it as if it was a real ethernet card.

Linux host
^^^^^^^^^^

As an example, you can download the ``linux-test-xxx.tar.gz`` archive
and copy the script ``qemu-ifup`` in ``/etc`` and configure properly
``sudo`` so that the command ``ifconfig`` contained in ``qemu-ifup`` can
be executed as root. You must verify that your host kernel supports the
TAP network interfaces: the device ``/dev/net/tun`` must be present.

See :ref:`sec_005finvocation` to have examples of command
lines using the TAP network interfaces.

Windows host
^^^^^^^^^^^^

There is a virtual ethernet driver for Windows 2000/XP systems, called
TAP-Win32. But it is not included in standard QEMU for Windows, so you
will need to get it separately. It is part of OpenVPN package, so
download OpenVPN from : https://openvpn.net/.

Using the user mode network stack
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

By using the option ``-net user`` (default configuration if no ``-net``
option is specified), QEMU uses a completely user mode network stack
(you don't need root privilege to use the virtual network). The virtual
network configuration is the following::

        guest (10.0.2.15)  <------>  Firewall/DHCP server <-----> Internet
                              |          (10.0.2.2)
                              |
                              ---->  DNS server (10.0.2.3)
                              |
                              ---->  SMB server (10.0.2.4)

The QEMU VM behaves as if it was behind a firewall which blocks all
incoming connections. You can use a DHCP client to automatically
configure the network in the QEMU VM. The DHCP server assign addresses
to the hosts starting from 10.0.2.15.

In order to check that the user mode network is working, you can ping
the address 10.0.2.2 and verify that you got an address in the range
10.0.2.x from the QEMU virtual DHCP server.

Note that ICMP traffic in general does not work with user mode
networking. ``ping``, aka. ICMP echo, to the local router (10.0.2.2)
shall work, however. If you're using QEMU on Linux >= 3.0, it can use
unprivileged ICMP ping sockets to allow ``ping`` to the Internet. The
host admin has to set the ping_group_range in order to grant access to
those sockets. To allow ping for GID 100 (usually users group)::

   echo 100 100 > /proc/sys/net/ipv4/ping_group_range

When using the built-in TFTP server, the router is also the TFTP server.

When using the ``'-netdev user,hostfwd=...'`` option, TCP or UDP
connections can be redirected from the host to the guest. It allows for
example to redirect X11, telnet or SSH connections.

Hubs
~~~~

QEMU can simulate several hubs. A hub can be thought of as a virtual
connection between several network devices. These devices can be for
example QEMU virtual ethernet cards or virtual Host ethernet devices
(TAP devices). You can connect guest NICs or host network backends to
such a hub using the ``-netdev
hubport`` or ``-nic hubport`` options. The legacy ``-net`` option also
connects the given device to the emulated hub with ID 0 (i.e. the
default hub) unless you specify a netdev with ``-net nic,netdev=xxx``
here.

Connecting emulated networks between QEMU instances
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Using the ``-netdev socket`` (or ``-nic socket`` or ``-net socket``)
option, it is possible to create emulated networks that span several
QEMU instances. See the description of the ``-netdev socket`` option in
:ref:`sec_005finvocation` to have a basic
example.
