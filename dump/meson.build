softmmu_ss.add([files('dump.c', 'dump-hmp-cmds.c'), snappy, lzo])
specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: files('win_dump.c'))
