Hyper-V Enlightenments
======================


Description
-----------

In some cases when implementing a hardware interface in software is slow, KVM
implements its own paravirtualized interfaces. This works well for Linux as
guest support for such features is added simultaneously with the feature itself.
It may, however, be hard-to-impossible to add support for these interfaces to
proprietary OSes, namely, Microsoft Windows.

KVM on x86 implements Hyper-V Enlightenments for Windows guests. These features
make Windows and Hyper-V guests think they're running on top of a Hyper-V
compatible hypervisor and use Hyper-V specific features.


Setup
-----

No Hyper-V enlightenments are enabled by default by either KVM or QEMU. In
QEMU, individual enlightenments can be enabled through CPU flags, e.g:

.. parsed-literal::

  |qemu_system| --enable-kvm --cpu host,hv_relaxed,hv_vpindex,hv_time, ...

Sometimes there are dependencies between enlightenments, QEMU is supposed to
check that the supplied configuration is sane.

When any set of the Hyper-V enlightenments is enabled, QEMU changes hypervisor
identification (CPUID 0x40000000..0x4000000A) to Hyper-V. KVM identification
and features are kept in leaves 0x40000100..0x40000101.


Existing enlightenments
-----------------------

``hv-relaxed``
  This feature tells guest OS to disable watchdog timeouts as it is running on a
  hypervisor. It is known that some Windows versions will do this even when they
  see 'hypervisor' CPU flag.

``hv-vapic``
  Provides so-called VP Assist page MSR to guest allowing it to work with APIC
  more efficiently. In particular, this enlightenment allows paravirtualized
  (exit-less) EOI processing.

``hv-spinlocks`` = xxx
  Enables paravirtualized spinlocks. The parameter indicates how many times
  spinlock acquisition should be attempted before indicating the situation to the
  hypervisor. A special value 0xffffffff indicates "never notify".

``hv-vpindex``
  Provides HV_X64_MSR_VP_INDEX (0x40000002) MSR to the guest which has Virtual
  processor index information. This enlightenment makes sense in conjunction with
  hv-synic, hv-stimer and other enlightenments which require the guest to know its
  Virtual Processor indices (e.g. when VP index needs to be passed in a
  hypercall).

``hv-runtime``
  Provides HV_X64_MSR_VP_RUNTIME (0x40000010) MSR to the guest. The MSR keeps the
  virtual processor run time in 100ns units. This gives guest operating system an
  idea of how much time was 'stolen' from it (when the virtual CPU was preempted
  to perform some other work).

``hv-crash``
  Provides HV_X64_MSR_CRASH_P0..HV_X64_MSR_CRASH_P5 (0x40000100..0x40000105) and
  HV_X64_MSR_CRASH_CTL (0x40000105) MSRs to the guest. These MSRs are written to
  by the guest when it crashes, HV_X64_MSR_CRASH_P0..HV_X64_MSR_CRASH_P5 MSRs
  contain additional crash information. This information is outputted in QEMU log
  and through QAPI.
  Note: unlike under genuine Hyper-V, write to HV_X64_MSR_CRASH_CTL causes guest
  to shutdown. This effectively blocks crash dump generation by Windows.

``hv-time``
  Enables two Hyper-V-specific clocksources available to the guest: MSR-based
  Hyper-V clocksource (HV_X64_MSR_TIME_REF_COUNT, 0x40000020) and Reference TSC
  page (enabled via MSR HV_X64_MSR_REFERENCE_TSC, 0x40000021). Both clocksources
  are per-guest, Reference TSC page clocksource allows for exit-less time stamp
  readings. Using this enlightenment leads to significant speedup of all timestamp
  related operations.

``hv-synic``
  Enables Hyper-V Synthetic interrupt controller - an extension of a local APIC.
  When enabled, this enlightenment provides additional communication facilities
  to the guest: SynIC messages and Events. This is a pre-requisite for
  implementing VMBus devices (not yet in QEMU). Additionally, this enlightenment
  is needed to enable Hyper-V synthetic timers. SynIC is controlled through MSRs
  HV_X64_MSR_SCONTROL..HV_X64_MSR_EOM (0x40000080..0x40000084) and
  HV_X64_MSR_SINT0..HV_X64_MSR_SINT15 (0x40000090..0x4000009F)

  Requires: ``hv-vpindex``

``hv-stimer``
  Enables Hyper-V synthetic timers. There are four synthetic timers per virtual
  CPU controlled through HV_X64_MSR_STIMER0_CONFIG..HV_X64_MSR_STIMER3_COUNT
  (0x400000B0..0x400000B7) MSRs. These timers can work either in single-shot or
  periodic mode. It is known that certain Windows versions revert to using HPET
  (or even RTC when HPET is unavailable) extensively when this enlightenment is
  not provided; this can lead to significant CPU consumption, even when virtual
  CPU is idle.

  Requires: ``hv-vpindex``, ``hv-synic``, ``hv-time``

``hv-tlbflush``
  Enables paravirtualized TLB shoot-down mechanism. On x86 architecture, remote
  TLB flush procedure requires sending IPIs and waiting for other CPUs to perform
  local TLB flush. In virtualized environment some virtual CPUs may not even be
  scheduled at the time of the call and may not require flushing (or, flushing
  may be postponed until the virtual CPU is scheduled). hv-tlbflush enlightenment
  implements TLB shoot-down through hypervisor enabling the optimization.

  Requires: ``hv-vpindex``

``hv-ipi``
  Enables paravirtualized IPI send mechanism. HvCallSendSyntheticClusterIpi
  hypercall may target more than 64 virtual CPUs simultaneously, doing the same
  through APIC requires more than one access (and thus exit to the hypervisor).

  Requires: ``hv-vpindex``

``hv-vendor-id`` = xxx
  This changes Hyper-V identification in CPUID 0x40000000.EBX-EDX from the default
  "Microsoft Hv". The parameter should be no longer than 12 characters. According
  to the specification, guests shouldn't use this information and it is unknown
  if there is a Windows version which acts differently.
  Note: hv-vendor-id is not an enlightenment and thus doesn't enable Hyper-V
  identification when specified without some other enlightenment.

``hv-reset``
  Provides HV_X64_MSR_RESET (0x40000003) MSR to the guest allowing it to reset
  itself by writing to it. Even when this MSR is enabled, it is not a recommended
  way for Windows to perform system reboot and thus it may not be used.

``hv-frequencies``
  Provides HV_X64_MSR_TSC_FREQUENCY (0x40000022) and HV_X64_MSR_APIC_FREQUENCY
  (0x40000023) allowing the guest to get its TSC/APIC frequencies without doing
  measurements.

``hv-reenlightenment``
  The enlightenment is nested specific, it targets Hyper-V on KVM guests. When
  enabled, it provides HV_X64_MSR_REENLIGHTENMENT_CONTROL (0x40000106),
  HV_X64_MSR_TSC_EMULATION_CONTROL (0x40000107)and HV_X64_MSR_TSC_EMULATION_STATUS
  (0x40000108) MSRs allowing the guest to get notified when TSC frequency changes
  (only happens on migration) and keep using old frequency (through emulation in
  the hypervisor) until it is ready to switch to the new one. This, in conjunction
  with ``hv-frequencies``, allows Hyper-V on KVM to pass stable clocksource
  (Reference TSC page) to its own guests.

  Note, KVM doesn't fully support re-enlightenment notifications and doesn't
  emulate TSC accesses after migration so 'tsc-frequency=' CPU option also has to
  be specified to make migration succeed. The destination host has to either have
  the same TSC frequency or support TSC scaling CPU feature.

  Recommended: ``hv-frequencies``

``hv-evmcs``
  The enlightenment is nested specific, it targets Hyper-V on KVM guests. When
  enabled, it provides Enlightened VMCS version 1 feature to the guest. The feature
  implements paravirtualized protocol between L0 (KVM) and L1 (Hyper-V)
  hypervisors making L2 exits to the hypervisor faster. The feature is Intel-only.

  Note: some virtualization features (e.g. Posted Interrupts) are disabled when
  hv-evmcs is enabled. It may make sense to measure your nested workload with and
  without the feature to find out if enabling it is beneficial.

  Requires: ``hv-vapic``

``hv-stimer-direct``
  Hyper-V specification allows synthetic timer operation in two modes: "classic",
  when expiration event is delivered as SynIC message and "direct", when the event
  is delivered via normal interrupt. It is known that nested Hyper-V can only
  use synthetic timers in direct mode and thus ``hv-stimer-direct`` needs to be
  enabled.

  Requires: ``hv-vpindex``, ``hv-synic``, ``hv-time``, ``hv-stimer``

``hv-avic`` (``hv-apicv``)
  The enlightenment allows to use Hyper-V SynIC with hardware APICv/AVIC enabled.
  Normally, Hyper-V SynIC disables these hardware feature and suggests the guest
  to use paravirtualized AutoEOI feature.
  Note: enabling this feature on old hardware (without APICv/AVIC support) may
  have negative effect on guest's performance.

``hv-no-nonarch-coresharing`` = on/off/auto
  This enlightenment tells guest OS that virtual processors will never share a
  physical core unless they are reported as sibling SMT threads. This information
  is required by Windows and Hyper-V guests to properly mitigate SMT related CPU
  vulnerabilities.

  When the option is set to 'auto' QEMU will enable the feature only when KVM
  reports that non-architectural coresharing is impossible, this means that
  hyper-threading is not supported or completely disabled on the host. This
  setting also prevents migration as SMT settings on the destination may differ.
  When the option is set to 'on' QEMU will always enable the feature, regardless
  of host setup. To keep guests secure, this can only be used in conjunction with
  exposing correct vCPU topology and vCPU pinning.

``hv-version-id-build``, ``hv-version-id-major``, ``hv-version-id-minor``, ``hv-version-id-spack``, ``hv-version-id-sbranch``, ``hv-version-id-snumber``
  This changes Hyper-V version identification in CPUID 0x40000002.EAX-EDX from the
  default (WS2016).

  - ``hv-version-id-build`` sets 'Build Number' (32 bits)
  - ``hv-version-id-major`` sets 'Major Version' (16 bits)
  - ``hv-version-id-minor`` sets 'Minor Version' (16 bits)
  - ``hv-version-id-spack`` sets 'Service Pack' (32 bits)
  - ``hv-version-id-sbranch`` sets 'Service Branch' (8 bits)
  - ``hv-version-id-snumber`` sets 'Service Number' (24 bits)

  Note: hv-version-id-* are not enlightenments and thus don't enable Hyper-V
  identification when specified without any other enlightenments.

``hv-syndbg``
  Enables Hyper-V synthetic debugger interface, this is a special interface used
  by Windows Kernel debugger to send the packets through, rather than sending
  them via serial/network .
  When enabled, this enlightenment provides additional communication facilities
  to the guest: SynDbg messages.
  This new communication is used by Windows Kernel debugger rather than sending
  packets via serial/network, adding significant performance boost over the other
  comm channels.
  This enlightenment requires a VMBus device (-device vmbus-bridge,irq=15).

  Requires: ``hv-relaxed``, ``hv_time``, ``hv-vapic``, ``hv-vpindex``, ``hv-synic``, ``hv-runtime``, ``hv-stimer``

``hv-emsr-bitmap``
  The enlightenment is nested specific, it targets Hyper-V on KVM guests. When
  enabled, it allows L0 (KVM) and L1 (Hyper-V) hypervisors to collaborate to
  avoid unnecessary updates to L2 MSR-Bitmap upon vmexits. While the protocol is
  supported for both VMX (Intel) and SVM (AMD), the VMX implementation requires
  Enlightened VMCS (``hv-evmcs``) feature to also be enabled.

  Recommended: ``hv-evmcs`` (Intel)

``hv-xmm-input``
  Hyper-V specification allows to pass parameters for certain hypercalls using XMM
  registers ("XMM Fast Hypercall Input"). When the feature is in use, it allows
  for faster hypercalls processing as KVM can avoid reading guest's memory.

``hv-tlbflush-ext``
  Allow for extended GVA ranges to be passed to Hyper-V TLB flush hypercalls
  (HvFlushVirtualAddressList/HvFlushVirtualAddressListEx).

  Requires: ``hv-tlbflush``

``hv-tlbflush-direct``
  The enlightenment is nested specific, it targets Hyper-V on KVM guests. When
  enabled, it allows L0 (KVM) to directly handle TLB flush hypercalls from L2
  guest without the need to exit to L1 (Hyper-V) hypervisor. While the feature is
  supported for both VMX (Intel) and SVM (AMD), the VMX implementation requires
  Enlightened VMCS (``hv-evmcs``) feature to also be enabled.

  Requires: ``hv-vapic``

  Recommended: ``hv-evmcs`` (Intel)

Supplementary features
----------------------

``hv-passthrough``
  In some cases (e.g. during development) it may make sense to use QEMU in
  'pass-through' mode and give Windows guests all enlightenments currently
  supported by KVM.

  Note: ``hv-passthrough`` flag only enables enlightenments which are known to QEMU
  (have corresponding 'hv-' flag) and copies ``hv-spinlocks`` and ``hv-vendor-id``
  values from KVM to QEMU. ``hv-passthrough`` overrides all other 'hv-' settings on
  the command line.

  Note: ``hv-passthrough`` does not enable ``hv-syndbg`` which can prevent certain
  Windows guests from booting when used without proper configuration. If needed,
  ``hv-syndbg`` can be enabled additionally.

  Note: ``hv-passthrough`` effectively prevents migration as the list of enabled
  enlightenments may differ between target and destination hosts.

``hv-enforce-cpuid``
  By default, KVM allows the guest to use all currently supported Hyper-V
  enlightenments when Hyper-V CPUID interface was exposed, regardless of if
  some features were not announced in guest visible CPUIDs. ``hv-enforce-cpuid``
  feature alters this behavior and only allows the guest to use exposed Hyper-V
  enlightenments.

Recommendations
---------------

To achieve the best performance of Windows and Hyper-V guests and unless there
are any specific requirements (e.g. migration to older QEMU/KVM versions,
emulating specific Hyper-V version, ...), it is recommended to enable all
currently implemented Hyper-V enlightenments with the following exceptions:

- ``hv-syndbg``, ``hv-passthrough``, ``hv-enforce-cpuid`` should not be enabled
  in production configurations as these are debugging/development features.
- ``hv-reset`` can be avoided as modern Hyper-V versions don't expose it.
- ``hv-evmcs`` can (and should) be enabled on Intel CPUs only. While the feature
  is only used in nested configurations (Hyper-V, WSL2), enabling it for regular
  Windows guests should not have any negative effects.
- ``hv-no-nonarch-coresharing`` must only be enabled if vCPUs are properly pinned
  so no non-architectural core sharing is possible.
- ``hv-vendor-id``, ``hv-version-id-build``, ``hv-version-id-major``,
  ``hv-version-id-minor``, ``hv-version-id-spack``, ``hv-version-id-sbranch``,
  ``hv-version-id-snumber`` can be left unchanged, guests are not supposed to
  behave differently when different Hyper-V version is presented to them.
- ``hv-crash`` must only be enabled if the crash information is consumed via
  QAPI by higher levels of the virtualization stack. Enabling this feature
  effectively prevents Windows from creating dumps upon crashes.
- ``hv-reenlightenment`` can only be used on hardware which supports TSC
  scaling or when guest migration is not needed.
- ``hv-spinlocks`` should be set to e.g. 0xfff when host CPUs are overcommited
  (meaning there are other scheduled tasks or guests) and can be left unchanged
  from the default value (0xffffffff) otherwise.
- ``hv-avic``/``hv-apicv`` should not be enabled if the hardware does not
  support APIC virtualization (Intel APICv, AMD AVIC).

Useful links
------------
Hyper-V Top Level Functional specification and other information:

- https://github.com/MicrosoftDocs/Virtualization-Documentation
- https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs

