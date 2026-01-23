system_ss.add([files('dump.c', 'dump-hmp-cmds.c'), snappy, lzo])
specific_ss.add(when: 'CONFIG_WINDUMP', if_true: files('win_dump-x86.c'))
system_ss.add(when: 'CONFIG_WINDUMP', if_false: files('win_dump-stubs.c'))
