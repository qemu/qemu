can_ss = ss.source_set()
can_ss.add(files('can_core.c', 'can_host.c'))
can_ss.add(when: 'CONFIG_LINUX', if_true: files('can_socketcan.c'))

system_ss.add_all(when: 'CONFIG_CAN_BUS', if_true: can_ss)
