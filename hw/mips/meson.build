mips_ss = ss.source_set()
mips_ss.add(files('addr.c', 'mips_int.c'))
mips_ss.add(when: 'CONFIG_FULOONG', if_true: files('fuloong2e.c'))
mips_ss.add(when: 'CONFIG_JAZZ', if_true: files('jazz.c'))
mips_ss.add(when: 'CONFIG_MALTA', if_true: files('gt64xxx_pci.c', 'malta.c'))
mips_ss.add(when: 'CONFIG_MIPSSIM', if_true: files('mipssim.c'))
mips_ss.add(when: 'CONFIG_MIPS_BOSTON', if_true: files('boston.c'))
mips_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('cps.c'))
mips_ss.add(when: 'CONFIG_R4K', if_true: files('r4k.c'))

hw_arch += {'mips': mips_ss}
