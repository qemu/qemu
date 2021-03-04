mips_ss = ss.source_set()
mips_ss.add(files('bootloader.c', 'mips_int.c'))
mips_ss.add(when: 'CONFIG_FW_CFG_MIPS', if_true: files('fw_cfg.c'))
mips_ss.add(when: 'CONFIG_FULOONG', if_true: files('fuloong2e.c'))
mips_ss.add(when: 'CONFIG_LOONGSON3V', if_true: files('loongson3_bootp.c', 'loongson3_virt.c'))
mips_ss.add(when: 'CONFIG_JAZZ', if_true: files('jazz.c'))
mips_ss.add(when: 'CONFIG_MALTA', if_true: files('gt64xxx_pci.c', 'malta.c'))
mips_ss.add(when: 'CONFIG_MIPSSIM', if_true: files('mipssim.c'))
mips_ss.add(when: 'CONFIG_MIPS_BOSTON', if_true: [files('boston.c'), fdt])
mips_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('cps.c'))

hw_arch += {'mips': mips_ss}
