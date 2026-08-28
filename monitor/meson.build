qmp_ss.add(files('monitor.c', 'qmp.c', 'qmp-cmds-control.c'))

system_ss.add(files(
  'fds.c',
  'qemu-config-qmp.c',
  'qmp-cmds.c',
))

if have_hmp
  system_ss.add(files(
    'hmp-cmds.c',
    'hmp.c',
  ))
endif
