Security
========

Overview
--------

This chapter explains the security requirements that QEMU is designed to meet
and principles for securely deploying QEMU.

Security Requirements
---------------------

QEMU supports many different use cases, some of which have stricter security
requirements than others.  The community has agreed on the overall security
requirements that users may depend on.  These requirements define what is
considered supported from a security perspective.

Virtualization Use Case
'''''''''''''''''''''''

The virtualization use case covers cloud and virtual private server (VPS)
hosting, as well as traditional data center and desktop virtualization.  These
use cases rely on hardware virtualization extensions to execute guest code
safely on the physical CPU at close-to-native speed.

The following entities are untrusted, meaning that they may be buggy or
malicious:

- Guest
- User-facing interfaces (e.g. VNC, SPICE, WebSocket)
- Network protocols (e.g. NBD, live migration)
- User-supplied files (e.g. disk images, kernels, device trees)
- Passthrough devices (e.g. PCI, USB)

Bugs affecting these entities are evaluated on whether they can cause damage in
real-world use cases and treated as security bugs if this is the case.

Non-virtualization Use Case
'''''''''''''''''''''''''''

The non-virtualization use case covers emulation using the Tiny Code Generator
(TCG).  In principle the TCG and device emulation code used in conjunction with
the non-virtualization use case should meet the same security requirements as
the virtualization use case.  However, for historical reasons much of the
non-virtualization use case code was not written with these security
requirements in mind.

Bugs affecting the non-virtualization use case are not considered security
bugs at this time.  Users with non-virtualization use cases must not rely on
QEMU to provide guest isolation or any security guarantees.

Architecture
------------

This section describes the design principles that ensure the security
requirements are met.

Guest Isolation
'''''''''''''''

Guest isolation is the confinement of guest code to the virtual machine.  When
guest code gains control of execution on the host this is called escaping the
virtual machine.  Isolation also includes resource limits such as throttling of
CPU, memory, disk, or network.  Guests must be unable to exceed their resource
limits.

QEMU presents an attack surface to the guest in the form of emulated devices.
The guest must not be able to gain control of QEMU.  Bugs in emulated devices
could allow malicious guests to gain code execution in QEMU.  At this point the
guest has escaped the virtual machine and is able to act in the context of the
QEMU process on the host.

Guests often interact with other guests and share resources with them.  A
malicious guest must not gain control of other guests or access their data.
Disk image files and network traffic must be protected from other guests unless
explicitly shared between them by the user.

Principle of Least Privilege
''''''''''''''''''''''''''''

The principle of least privilege states that each component only has access to
the privileges necessary for its function.  In the case of QEMU this means that
each process only has access to resources belonging to the guest.

The QEMU process should not have access to any resources that are inaccessible
to the guest.  This way the guest does not gain anything by escaping into the
QEMU process since it already has access to those same resources from within
the guest.

Following the principle of least privilege immediately fulfills guest isolation
requirements.  For example, guest A only has access to its own disk image file
``a.img`` and not guest B's disk image file ``b.img``.

In reality certain resources are inaccessible to the guest but must be
available to QEMU to perform its function.  For example, host system calls are
necessary for QEMU but are not exposed to guests.  A guest that escapes into
the QEMU process can then begin invoking host system calls.

New features must be designed to follow the principle of least privilege.
Should this not be possible for technical reasons, the security risk must be
clearly documented so users are aware of the trade-off of enabling the feature.

Isolation mechanisms
''''''''''''''''''''

Several isolation mechanisms are available to realize this architecture of
guest isolation and the principle of least privilege.  With the exception of
Linux seccomp, these mechanisms are all deployed by management tools that
launch QEMU, such as libvirt.  They are also platform-specific so they are only
described briefly for Linux here.

The fundamental isolation mechanism is that QEMU processes must run as
unprivileged users.  Sometimes it seems more convenient to launch QEMU as
root to give it access to host devices (e.g. ``/dev/net/tun``) but this poses a
huge security risk.  File descriptor passing can be used to give an otherwise
unprivileged QEMU process access to host devices without running QEMU as root.
It is also possible to launch QEMU as a non-root user and configure UNIX groups
for access to ``/dev/kvm``, ``/dev/net/tun``, and other device nodes.
Some Linux distros already ship with UNIX groups for these devices by default.

- SELinux and AppArmor make it possible to confine processes beyond the
  traditional UNIX process and file permissions model.  They restrict the QEMU
  process from accessing processes and files on the host system that are not
  needed by QEMU.

- Resource limits and cgroup controllers provide throughput and utilization
  limits on key resources such as CPU time, memory, and I/O bandwidth.

- Linux namespaces can be used to make process, file system, and other system
  resources unavailable to QEMU.  A namespaced QEMU process is restricted to only
  those resources that were granted to it.

- Linux seccomp is available via the QEMU ``--sandbox`` option.  It disables
  system calls that are not needed by QEMU, thereby reducing the host kernel
  attack surface.

Sensitive configurations
------------------------

There are aspects of QEMU that can have security implications which users &
management applications must be aware of.

Monitor console (QMP and HMP)
'''''''''''''''''''''''''''''

The monitor console (whether used with QMP or HMP) provides an interface
to dynamically control many aspects of QEMU's runtime operation. Many of the
commands exposed will instruct QEMU to access content on the host file system
and/or trigger spawning of external processes.

For example, the ``migrate`` command allows for the spawning of arbitrary
processes for the purpose of tunnelling the migration data stream. The
``blockdev-add`` command instructs QEMU to open arbitrary files, exposing
their content to the guest as a virtual disk.

Unless QEMU is otherwise confined using technologies such as SELinux, AppArmor,
or Linux namespaces, the monitor console should be considered to have privileges
equivalent to those of the user account QEMU is running under.

It is further important to consider the security of the character device backend
over which the monitor console is exposed. It needs to have protection against
malicious third parties which might try to make unauthorized connections, or
perform man-in-the-middle attacks. Many of the character device backends do not
satisfy this requirement and so must not be used for the monitor console.

The general recommendation is that the monitor console should be exposed over
a UNIX domain socket backend to the local host only. Use of the TCP based
character device backend is inappropriate unless configured to use both TLS
encryption and authorization control policy on client connections.

In summary, the monitor console is considered a privileged control interface to
QEMU and as such should only be made accessible to a trusted management
application or user.
