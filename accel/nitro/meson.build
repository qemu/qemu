nitro_ss = ss.source_set()
nitro_ss.add(files('nitro-accel.c'))
system_ss.add_all(when: 'CONFIG_NITRO', if_true: nitro_ss)
