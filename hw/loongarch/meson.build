loongarch_ss = ss.source_set()
loongarch_ss.add(files(
    'fw_cfg.c',
))
loongarch_ss.add(when: 'CONFIG_LOONGARCH_VIRT', if_true: files('loongson3.c'))
loongarch_ss.add(when: 'CONFIG_ACPI', if_true: files('acpi-build.c'))

hw_arch += {'loongarch': loongarch_ss}
