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

To be covered by this security support policy you must:

- use a virtualization accelerator like KVM or HVF
- use one of the machine types listed below

It may be possible to use other machine types with a virtualization
accelerator to provide improved performance with a trusted guest
workload, but any machine type not listed here should not be
considered to be providing guest isolation or security guarantees,
and falls under the "non-virtualization use case".

Supported machine types for the virtualization use case, by target architecture:

aarch64
  ``virt``
i386, x86_64
  ``microvm``, ``xenfv``, ``xenpv``, ``xenpvh``, ``pc``, ``q35``
s390x
  ``s390-ccw-virtio``
loongarch64:
  ``virt``
ppc64:
  ``pseries``
riscv32, riscv64:
  ``virt``

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

Security boundary scope
'''''''''''''''''''''''

Even where a flaw affects the virtualization use case described above,
not all scenarios will be considered in scope. The following guidelines
are used to evaluate whether to apply the full security process, or treat
an issue as a normal bug.

* **assert** / **abort**. If triggering the code path requires kernel
  privileges (or root account access) in the guest, asserts/aborts in
  QEMU are a self inflicted denial of service. These will **not** be
  treated as security flaws, at most hardening bugs. If triggering the
  code path can be done by an unprivileged guest OS account, this
  **may** justify handling as a security bug.

* **vhost-user/vfio-user backends**. The backend processes have
  shared memory regions co-mapped with the QEMU process. The intent
  of the process separation is operational resilience & flexibility
  and allowing for independent software suppliers. There is not
  considered to be security boundary between QEMU and the vhost-user
  & vfio-user backends. Thus flaws in the backends which can cause
  crashes / undesirable behaviour in QEMU will **not** be treated as
  security flaws, but should be fixed as hardening bugs.

* **memory allocation bounds**. There are many ways in which a QEMU
  process can legitimately consume an amount of memory that is
  significantly larger than the assigned guest RAM. QEMU's worst
  case memory usage should be considered effectively unbounded. As
  such the QEMU deployment on the host should account for the
  possibility of large memory peaks and apply countermeasures to
  provide continuity of host operations. It is typical for the Linux
  OOM killer to reap the process triggering host memory overcommit
  in the case of exccessive usage, offering a degree of protection.
  As such, bugs which can lead to excessive/unbounded memory allocations
  will usually not be classified as security flaws, but should be
  fixed as hardening bugs.

* **degraded guest behaviour**. There are a set of bugs which can
  lead guest hardware devices to misbehave. For example, a flawed
  virtual IOMMU operation may not offer the guest device isolation
  that would otherwise be expected. If a guest triggered exploit
  requires kernel privileges (or root account access), and leads
  to sub-optimal behaviour of the virtual device this is considered
  a self inflicted service degradation. These will **not** be
  treated as security flaws, at most hardening bugs. If triggering
  the code path can be done by an unprivileged guest OS account,
  this may justify handling as a security bug.

* **nested virtualization**. The scope for nested virtualization
  is to prevent a level 2 guest from breaking out into a level
  1 guest. As noted above, a number of scenarios exclude security
  handling for flaws only exploitable by the guest kernel / root
  account with affect the guest's own service/availability. In the
  context of nested virtualization with PCI device assignment, it
  may may be possible for a level 2 guest kernel to trigger flaws
  that affect the level 0 QEMU process. While these bugs should be
  fixed, they will not be triaged as security flaws at this time.

* **migration/snapshots**. Migration failures and snapshot load
  failures are considered part of normal operation as long as the
  source virtual machine and savevm file, respectively, are still
  functional. Aborting the QEMU process at the migration/snapshot
  destination is similarly not considered a security issue. The
  migration stream is assumed to be secure as long as the design
  principles described in the Architecture section are held, in
  which case plain manipulation of the stream is not considered as
  an attack vector.

* **low severity impact**. As a catch all rule, issues which
  are judged to have a "low" severity impact on the system will
  usually not justify handling as security bugs, nor assignment
  of CVEs. They will be fixed as routine bugs when time allows.

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

Guests often interact with other guests and share resources with them.
A malicious guest must not gain control of other guests or access
their data.  Disk image files and network traffic must be protected
from other guests, users and processes unless explicitly shared with
them by the user.

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

- Transport Layer Security (TLS) protocol can be used to ensure authenticity and
  encryption of the live migration connection where the network is untrusted.

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
