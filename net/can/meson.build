can_ss = ss.source_set()
can_ss.add(files('can_core.c', 'can_host.c'))
if host_os == 'linux'
  can_ss.add(files('can_socketcan.c'))
endif

system_ss.add_all(when: 'CONFIG_CAN_BUS', if_true: can_ss)
