i386_mshv_ss = ss.source_set()
i386_mshv_ss.add(files('clock.c'))
i386_mshv_ss.add(when: 'CONFIG_APIC', if_true: files('apic.c'))

i386_ss.add_all(when: 'CONFIG_MSHV', if_true: i386_mshv_ss)
