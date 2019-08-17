cris_ss = ss.source_set()
cris_ss.add(files('boot.c'))
cris_ss.add(when: 'CONFIG_AXIS', if_true: files('axis_dev88.c'))

hw_arch += {'cris': cris_ss}
