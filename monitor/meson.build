qmp_ss.add(files('monitor.c', 'qmp.c', 'qmp-cmds-control.c'))

system_ss.add(files(
  'fds.c',
  'hmp-cmds.c',
  'hmp.c',
  'qemu-config-qmp.c',
))
system_ss.add([spice_headers, files('qmp-cmds.c')])

specific_ss.add(when: 'CONFIG_SYSTEM_ONLY',
		if_true: [files( 'hmp-cmds-target.c', 'hmp-target.c'), spice])
