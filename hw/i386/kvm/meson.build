i386_kvm_ss = ss.source_set()
i386_kvm_ss.add(files('clock.c'))
i386_kvm_ss.add(when: 'CONFIG_APIC', if_true: files('apic.c'))
i386_kvm_ss.add(when: 'CONFIG_I8254', if_true: files('i8254.c'))
i386_kvm_ss.add(when: 'CONFIG_I8259', if_true: files('i8259.c'))
i386_kvm_ss.add(when: 'CONFIG_IOAPIC', if_true: files('ioapic.c'))
i386_kvm_ss.add(when: 'CONFIG_XEN_EMU', if_true: files(
  'xen_overlay.c',
  'xen_evtchn.c',
  'xen_gnttab.c',
  'xen_xenstore.c',
  'xenstore_impl.c',
  ))

i386_ss.add_all(when: 'CONFIG_KVM', if_true: i386_kvm_ss)

xen_stubs_ss = ss.source_set()
xen_stubs_ss.add(when: 'CONFIG_XEN_EMU', if_false: files(
  'xen-stubs.c',
))

specific_ss.add_all(when: 'CONFIG_SYSTEM_ONLY', if_true: xen_stubs_ss)
