Paravirtualized KVM features
============================

Description
-----------

In some cases when implementing hardware interfaces in software is slow, ``KVM``
implements its own paravirtualized interfaces.

Setup
-----

Paravirtualized ``KVM`` features are represented as CPU flags. The following
features are enabled by default for any CPU model when ``KVM`` acceleration is
enabled:

- ``kvmclock``
- ``kvm-nopiodelay``
- ``kvm-asyncpf``
- ``kvm-steal-time``
- ``kvm-pv-eoi``
- ``kvmclock-stable-bit``

``kvm-msi-ext-dest-id`` feature is enabled by default in x2apic mode with split
irqchip (e.g. "-machine ...,kernel-irqchip=split -cpu ...,x2apic").

Note: when CPU model ``host`` is used, QEMU passes through all supported
paravirtualized ``KVM`` features to the guest.

Existing features
-----------------

``kvmclock``
  Expose a ``KVM`` specific paravirtualized clocksource to the guest. Supported
  since Linux v2.6.26.

``kvm-nopiodelay``
  The guest doesn't need to perform delays on PIO operations. Supported since
  Linux v2.6.26.

``kvm-mmu``
  This feature is deprecated.

``kvm-asyncpf``
  Enable asynchronous page fault mechanism. Supported since Linux v2.6.38.
  Note: since Linux v5.10 the feature is deprecated and not enabled by ``KVM``.
  Use ``kvm-asyncpf-int`` instead.

``kvm-steal-time``
  Enable stolen (when guest vCPU is not running) time accounting. Supported
  since Linux v3.1.

``kvm-pv-eoi``
  Enable paravirtualized end-of-interrupt signaling. Supported since Linux
  v3.10.

``kvm-pv-unhalt``
  Enable paravirtualized spinlocks support. Supported since Linux v3.12.

``kvm-pv-tlb-flush``
  Enable paravirtualized TLB flush mechanism. Supported since Linux v4.16.

``kvm-pv-ipi``
  Enable paravirtualized IPI mechanism. Supported since Linux v4.19.

``kvm-poll-control``
  Enable host-side polling on HLT control from the guest. Supported since Linux
  v5.10.

``kvm-pv-sched-yield``
  Enable paravirtualized sched yield feature. Supported since Linux v5.10.

``kvm-asyncpf-int``
  Enable interrupt based asynchronous page fault mechanism. Supported since Linux
  v5.10.

``kvm-msi-ext-dest-id``
  Support 'Extended Destination ID' for external interrupts. The feature allows
  to use up to 32768 CPUs without IRQ remapping (but other limits may apply making
  the number of supported vCPUs for a given configuration lower). Supported since
  Linux v5.10.

``kvmclock-stable-bit``
  Tell the guest that guest visible TSC value can be fully trusted for kvmclock
  computations and no warps are expected. Supported since Linux v2.6.35.

Supplementary features
----------------------

``kvm-pv-enforce-cpuid``
  Limit the supported paravirtualized feature set to the exposed features only.
  Note, by default, ``KVM`` allows the guest to use all currently supported
  paravirtualized features even when they were not announced in guest visible
  CPUIDs. Supported since Linux v5.10.


Useful links
------------

Please refer to Documentation/virt/kvm in Linux for additional details.
