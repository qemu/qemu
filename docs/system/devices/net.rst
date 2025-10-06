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

A distribution will generally provide specific helper scripts when it
packages QEMU. By default these are found at ``/etc/qemu-ifup`` and
``/etc/qemu-ifdown`` and are called appropriately when QEMU wants to
change the network state.

If QEMU is being run as a non-privileged user you may need properly
configure ``sudo`` so that network commands in the scripts can be
executed as root.

You must verify that your host kernel supports the TAP network
interfaces: the device ``/dev/net/tun`` must be present.

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

When using the ``'-netdev user,hostfwd=...'`` option, TCP, UDP or UNIX
connections can be redirected from the host to the guest. It allows for
example to redirect X11, telnet or SSH connections.

Using passt as the user mode network stack
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

passt_ can be used as a simple replacement for SLIRP (``-net user``).
passt doesn't require any capability or privilege. passt has
better performance than ``-net user``, full IPv6 support and better security
as it's a daemon that is not executed in QEMU context.

passt_ can be used in the same way as the user backend (using ``-net passt``,
``-netdev passt`` or ``-nic passt``) or it can be launched manually and
connected to QEMU either by using a socket (``-netdev stream``) or by using
the vhost-user interface (``-netdev vhost-user``).

Using ``-netdev stream`` or ``-netdev vhost-user`` will allow the user to
enable functionalities not available through the passt backend interface
(like migration).

See `passt(1)`_ for more details on passt.

.. _passt: https://passt.top/
.. _passt(1): https://passt.top/builds/latest/web/passt.1.html

To use the passt backend interface
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There is no need to start the daemon as QEMU will do it for you.

By default, passt will be started in the socket-based mode.

.. parsed-literal::
   |qemu_system| [...OPTIONS...] -nic passt

   (qemu) info network
   e1000e.0: index=0,type=nic,model=e1000e,macaddr=52:54:00:12:34:56
    \ #net071: index=0,type=passt,stream,connected to pid 24846

.. parsed-literal::
   |qemu_system| [...OPTIONS...] -net nic -net passt,tcp-ports=10001,udp-ports=10001

   (qemu) info network
   hub 0
    \ hub0port1: #net136: index=0,type=passt,stream,connected to pid 25204
    \ hub0port0: e1000e.0: index=0,type=nic,model=e1000e,macaddr=52:54:00:12:34:56

.. parsed-literal::
   |qemu_system| [...OPTIONS...] -netdev passt,id=netdev0 -device virtio-net,mac=9a:2b:2c:2d:2e:2f,id=virtio0,netdev=netdev0

   (qemu) info network
   virtio0: index=0,type=nic,model=virtio-net-pci,macaddr=9a:2b:2c:2d:2e:2f
    \ netdev0: index=0,type=passt,stream,connected to pid 25428

To use the vhost-based interface, add the ``vhost-user=on`` parameter and
select the virtio-net device:

.. parsed-literal::
   |qemu_system| [...OPTIONS...] -nic passt,model=virtio,vhost-user=on

   (qemu) info network
   virtio-net-pci.0: index=0,type=nic,model=virtio-net-pci,macaddr=52:54:00:12:34:56
    \ #net006: index=0,type=passt,vhost-user,connected to pid 25731

To use socket based passt interface:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Start passt as a daemon::

   passt --socket ~/passt.socket

If ``--socket`` is not provided, passt will print the path of the UNIX domain socket QEMU can connect to (``/tmp/passt_1.socket``, ``/tmp/passt_2.socket``,
...). Then you can connect your QEMU instance to passt:

.. parsed-literal::
   |qemu_system| [...OPTIONS...] -device virtio-net-pci,netdev=netdev0 -netdev stream,id=netdev0,server=off,addr.type=unix,addr.path=~/passt.socket

Where ``~/passt.socket`` is the UNIX socket created by passt to
communicate with QEMU.

To use vhost-based interface:
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Start passt with ``--vhost-user``::

   passt --vhost-user --socket ~/passt.socket

Then to connect QEMU:

.. parsed-literal::
   |qemu_system| [...OPTIONS...] -m $RAMSIZE -chardev socket,id=chr0,path=~/passt.socket -netdev vhost-user,id=netdev0,chardev=chr0 -device virtio-net,netdev=netdev0 -object memory-backend-memfd,id=memfd0,share=on,size=$RAMSIZE -numa node,memdev=memfd0

Where ``$RAMSIZE`` is the memory size of your VM ``-m`` and ``-object memory-backend-memfd,size=`` must match.

Migration of passt:
^^^^^^^^^^^^^^^^^^^

When passt is connected to QEMU using the vhost-user interface it can
be migrated with QEMU and the network connections are not interrupted.

As passt runs with no privileges, it relies on passt-repair to save and
load the TCP connections state, using the TCP_REPAIR socket option.
The passt-repair helper needs to have the CAP_NET_ADMIN capability, or run as root. If passt-repair is not available, TCP connections will not be preserved.

Example of migration of a guest on the same host
________________________________________________

Before being able to run passt-repair, the CAP_NET_ADMIN capability must be set
on the file, run as root::

   setcap cap_net_admin+eip ./passt-repair

Start passt for the source side::

   passt --vhost-user --socket ~/passt_src.socket --repair-path ~/passt-repair_src.socket

Where ``~/passt-repair_src.socket`` is the UNIX socket created by passt to
communicate with passt-repair. The default value is the ``--socket`` path
appended with ``.repair``.

Start passt-repair::

   passt-repair ~/passt-repair_src.socket

Start source side QEMU with a monitor to be able to send the migrate command:

.. parsed-literal::
   |qemu_system| [...OPTIONS...] [...VHOST USER OPTIONS...] -monitor stdio

Start passt for the destination side::

   passt --vhost-user --socket ~/passt_dst.socket --repair-path ~/passt-repair_dst.socket

Start passt-repair::

   passt-repair ~/passt-repair_dst.socket

Start QEMU with the ``-incoming`` parameter:

.. parsed-literal::
   |qemu_system| [...OPTIONS...] [...VHOST USER OPTIONS...] -incoming tcp:localhost:4444

Then in the source guest monitor the migration can be started::

   (qemu) migrate tcp:localhost:4444

A separate passt-repair instance must be started for every migration. In the case of a failed migration, passt-repair also needs to be restarted before trying
again.

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
