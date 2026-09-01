qom_ss.add(genh)
qom_ss.add(files(
  'container.c',
  'object.c',
  'object_interfaces.c',
  'qom-qobject.c',
))
if have_system
  qom_ss.add(files('compat-properties.c'))
endif

qmp_ss.add(files('qom-qmp-cmds.c'))
if have_hmp
  system_ss.add(files('qom-hmp-cmds.c'))
endif
