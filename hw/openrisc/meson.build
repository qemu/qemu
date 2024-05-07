openrisc_ss = ss.source_set()
openrisc_ss.add(files('cputimer.c'))
openrisc_ss.add(files('boot.c'))
openrisc_ss.add(when: 'CONFIG_OR1K_SIM', if_true: files('openrisc_sim.c'))
openrisc_ss.add(when: 'CONFIG_OR1K_VIRT', if_true: files('virt.c'))

hw_arch += {'openrisc': openrisc_ss}
