sh4_ss = ss.source_set()
sh4_ss.add(files(
  'sh7750.c',
  'sh7750_regnames.c',
  'sh_pci.c'
))
sh4_ss.add(when: 'CONFIG_R2D', if_true: files('r2d.c'))
sh4_ss.add(when: 'CONFIG_SHIX', if_true: files('shix.c'))

hw_arch += {'sh4': sh4_ss}
