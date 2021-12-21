=====
D-Bus
=====

Introduction
============

QEMU may be running with various helper processes involved:
 - vhost-user* processes (gpu, virtfs, input, etc...)
 - TPM emulation (or other devices)
 - user networking (slirp)
 - network services (DHCP/DNS, samba/ftp etc)
 - background tasks (compression, streaming etc)
 - client UI
 - admin & cli

Having several processes allows stricter security rules, as well as
greater modularity.

While QEMU itself uses QMP as primary IPC (and Spice/VNC for remote
display), D-Bus is the de facto IPC of choice on Unix systems. The
wire format is machine friendly, good bindings exist for various
languages, and there are various tools available.

Using a bus, helper processes can discover and communicate with each
other easily, without going through QEMU. The bus topology is also
easier to apprehend and debug than a mesh. However, it is wise to
consider the security aspects of it.

Security
========

A QEMU D-Bus bus should be private to a single VM. Thus, only
cooperative tasks are running on the same bus to serve the VM.

D-Bus, the protocol and standard, doesn't have mechanisms to enforce
security between peers once the connection is established. Peers may
have additional mechanisms to enforce security rules, based for
example on UNIX credentials.

The daemon can control which peers can send/recv messages using
various metadata attributes, however, this is alone is not generally
sufficient to make the deployment secure.  The semantics of the actual
methods implemented using D-Bus are just as critical. Peers need to
carefully validate any information they received from a peer with a
different trust level.

dbus-daemon policy
------------------

dbus-daemon can enforce various policies based on the UID/GID of the
processes that are connected to it. It is thus a good idea to run
helpers as different UID from QEMU and set appropriate policies.

Depending on the use case, you may choose different scenarios:

 - Everything the same UID

   - Convenient for developers
   - Improved reliability - crash of one part doesn't take
     out entire VM
   - No security benefit over traditional QEMU, unless additional
     unless additional controls such as SELinux or AppArmor are
     applied

 - Two UIDs, one for QEMU, one for dbus & helpers

   - Moderately improved user based security isolation

 - Many UIDs, one for QEMU one for dbus and one for each helpers

   - Best user based security isolation
   - Complex to manager distinct UIDs needed for each VM

For example, to allow only ``qemu`` user to talk to ``qemu-helper``
``org.qemu.Helper1`` service, a dbus-daemon policy may contain:

.. code:: xml

  <policy user="qemu">
     <allow send_destination="org.qemu.Helper1"/>
     <allow receive_sender="org.qemu.Helper1"/>
  </policy>

  <policy user="qemu-helper">
     <allow own="org.qemu.Helper1"/>
  </policy>


dbus-daemon can also perform SELinux checks based on the security
context of the source and the target. For example, ``virtiofs_t``
could be allowed to send a message to ``svirt_t``, but ``virtiofs_t``
wouldn't be allowed to send a message to ``virtiofs_t``.

See dbus-daemon man page for details.

Guidelines
==========

When implementing new D-Bus interfaces, it is recommended to follow
the "D-Bus API Design Guidelines":
https://dbus.freedesktop.org/doc/dbus-api-design.html

The "org.qemu.*" prefix is reserved for services implemented &
distributed by the QEMU project.

QEMU Interfaces
===============

:doc:`dbus-vmstate`

:doc:`dbus-display`
