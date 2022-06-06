loongarch_ss = ss.source_set()
loongarch_ss.add(when: 'CONFIG_LOONGARCH_VIRT', if_true: files('loongson3.c'))

hw_arch += {'loongarch': loongarch_ss}
