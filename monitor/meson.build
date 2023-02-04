qmp_ss.add(files('monitor.c', 'qmp.c', 'qmp-cmds-control.c'))

softmmu_ss.add(files(
  'fds.c',
  'hmp-cmds.c',
  'hmp.c',
))
softmmu_ss.add([spice_headers, files('qmp-cmds.c')])

specific_ss.add(when: 'CONFIG_SOFTMMU',
		if_true: [files( 'hmp-cmds-target.c', 'hmp-target.c'), spice])
