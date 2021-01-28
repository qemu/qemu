nios2_ss = ss.source_set()
nios2_ss.add(files('boot.c'))
nios2_ss.add(when: 'CONFIG_NIOS2_10M50', if_true: files('10m50_devboard.c'))
nios2_ss.add(when: 'CONFIG_NIOS2_GENERIC_NOMMU', if_true: files('generic_nommu.c'))

hw_arch += {'nios2': nios2_ss}
