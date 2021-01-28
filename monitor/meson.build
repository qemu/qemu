qmp_ss.add(files('monitor.c', 'qmp.c', 'qmp-cmds-control.c'))

softmmu_ss.add(files(
  'hmp-cmds.c',
  'hmp.c',
))
softmmu_ss.add([spice_headers, files('qmp-cmds.c')])

specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files('misc.c'), spice])
