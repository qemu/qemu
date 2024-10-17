loongarch_ss = ss.source_set()
loongarch_ss.add(files(
    'boot.c',
))
common_ss.add(when: 'CONFIG_LOONGARCH_VIRT', if_true: files('fw_cfg.c'))
loongarch_ss.add(when: 'CONFIG_LOONGARCH_VIRT', if_true: files('virt.c'))
loongarch_ss.add(when: 'CONFIG_ACPI', if_true: files('acpi-build.c'))

hw_arch += {'loongarch': loongarch_ss}
