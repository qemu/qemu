sysemu_stubs_ss = ss.source_set()
sysemu_stubs_ss.add(when: 'CONFIG_HAX', if_false: files('hax-stub.c'))
sysemu_stubs_ss.add(when: 'CONFIG_XEN', if_false: files('xen-stub.c'))
sysemu_stubs_ss.add(when: 'CONFIG_KVM', if_false: files('kvm-stub.c'))
sysemu_stubs_ss.add(when: 'CONFIG_TCG', if_false: files('tcg-stub.c'))

specific_ss.add_all(when: ['CONFIG_SYSTEM_ONLY'], if_true: sysemu_stubs_ss)
