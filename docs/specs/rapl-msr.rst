================
RAPL MSR support
================

The RAPL interface (Running Average Power Limit) is advertising the accumulated
energy consumption of various power domains (e.g. CPU packages, DRAM, etc.).

The consumption is reported via MSRs (model specific registers) like
MSR_PKG_ENERGY_STATUS for the CPU package power domain. These MSRs are 64 bits
registers that represent the accumulated energy consumption in micro Joules.

Thanks to KVM's `MSR filtering <msr-filter-patch_>`__ functionality,
not all MSRs are handled by KVM. Some of them can now be handled by the
userspace (QEMU); a list of MSRs is given at VM creation time to KVM, and
a userspace exit occurs when they are accessed.

.. _msr-filter-patch: https://patchwork.kernel.org/project/kvm/patch/20200916202951.23760-7-graf@amazon.com/

At the moment the following MSRs are involved:

.. code:: C

    #define MSR_RAPL_POWER_UNIT             0x00000606
    #define MSR_PKG_POWER_LIMIT             0x00000610
    #define MSR_PKG_ENERGY_STATUS           0x00000611
    #define MSR_PKG_POWER_INFO              0x00000614

The ``*_POWER_UNIT``, ``*_POWER_LIMIT``, ``*_POWER INFO`` are part of the RAPL
spec and specify the power limit of the package, provide range of parameter(min
power, max power,..) and also the information of the multiplier for the energy
counter to calculate the power. Those MSRs are populated once at the beginning
by reading the host CPU MSRs and are given back to the guest 1:1 when
requested.

The MSR_PKG_ENERGY_STATUS is a counter; it represents the total amount of
energy consumed since the last time the register was cleared. If you multiply
it with the UNIT provided above you'll get the power in micro-joules. This
counter is always increasing and it increases more or less faster depending on
the consumption of the package. This counter is supposed to overflow at some
point.

Each core belonging to the same Package reading the MSR_PKG_ENERGY_STATUS (i.e
"rdmsr 0x611") will retrieve the same value. The value represents the energy
for the whole package. Whatever Core reading it will get the same value and a
core that belongs to PKG-0 will not be able to get the value of PKG-1 and
vice-versa.

High level implementation
-------------------------

In order to update the value of the virtual MSR, a QEMU thread is created.
The thread is basically just an infinity loop that does:

1. Snapshot of the time metrics of all QEMU threads (Time spent scheduled in
   Userspace and System)

2. Snapshot of the actual MSR_PKG_ENERGY_STATUS counter of all packages where
   the QEMU threads are running on.

3. Sleep for 1 second - During this pause the vcpu and other non-vcpu threads
   will do what they have to do and so the energy counter will increase.

4. Repeat 2. and 3. and calculate the delta of every metrics representing the
   time spent scheduled for each QEMU thread *and* the energy spent by the
   packages during the pause.

5. Filter the vcpu threads and the non-vcpu threads.

6. Retrieve the topology of the Virtual Machine. This helps identify which
   vCPU is running on which virtual package.

7. The total energy spent by the non-vcpu threads is divided by the number
   of vcpu threads so that each vcpu thread will get an equal part of the
   energy spent by the QEMU workers.

8. Calculate the ratio of energy spent per vcpu threads.

9. Calculate the energy for each virtual package.

10. The virtual MSRs are updated for each virtual package. Each vCPU that
    belongs to the same package will return the same value when accessing the
    the MSR.

11. Loop back to 1.

Ratio calculation
-----------------

In Linux, a process has an execution time associated with it. The scheduler is
dividing the time in clock ticks. The number of clock ticks per second can be
found by the sysconf system call. A typical value of clock ticks per second is
100. So a core can run a process at the maximum of 100 ticks per second. If a
package has 4 cores, 400 ticks maximum can be scheduled on all the cores
of the package for a period of 1 second.

`/proc/[pid]/stat <stat_>`__ is a procfs file that can give the executed
time of a process with the [pid] as the process ID. It gives the amount
of ticks the process has been scheduled in userspace (utime) and kernel
space (stime).

.. _stat: https://man7.org/linux/man-pages/man5/proc.5.html

By reading those metrics for a thread, one can calculate the ratio of time the
package has spent executing the thread.

Example:

A 4 cores package can schedule a maximum of 400 ticks per second with 100 ticks
per second per core. If a thread was scheduled for 100 ticks between a second
on this package, that means my thread has been scheduled for 1/4 of the whole
package. With that, the calculation of the energy spent by the thread on this
package during this whole second is 1/4 of the total energy spent by the
package.

Usage
-----

Currently this feature is only working on an Intel CPU that has the RAPL driver
mounted and available in the sysfs. if not, QEMU fails at start-up.

This feature is activated with -accel
kvm,rapl=true,rapl-helper-socket=/path/sock.sock

It is important that the socket path is the same as the one
:program:`qemu-vmsr-helper` is listening to.

qemu-vmsr-helper
----------------

The qemu-vmsr-helper is working very much like the qemu-pr-helper. Instead of
making persistent reservation, qemu-vmsr-helper is here to overcome the
CVE-2020-8694 which remove user access to the rapl msr attributes.

A socket communication is established between QEMU processes that has the RAPL
MSR support activated and the qemu-vmsr-helper. A systemd service and socket
activation is provided in contrib/systemd/qemu-vmsr-helper.(service/socket).

The systemd socket uses 600, like contrib/systemd/qemu-pr-helper.socket. The
socket can be passed via SCM_RIGHTS by libvirt, or its permissions can be
changed (e.g. 660 and root:kvm for a Debian system for example). Libvirt could
also start a separate helper if needed. All in all, the policy is left to the
user.

See the qemu-pr-helper documentation or manpage for further details.

Current Limitations
-------------------

- Works only on Intel host CPUs because AMD CPUs are using different MSR
  addresses.

- Only the Package Power-Plane (MSR_PKG_ENERGY_STATUS) is reported at the
  moment.

