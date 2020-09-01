openrisc_ss = ss.source_set()
openrisc_ss.add(files('pic_cpu.c', 'cputimer.c'))
openrisc_ss.add(when: 'CONFIG_OR1K_SIM', if_true: files('openrisc_sim.c'))

hw_arch += {'openrisc': openrisc_ss}
