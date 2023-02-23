softmmu_ss.add(files('dump-hmp-cmds.c'))

specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files('dump.c'), snappy, lzo])
specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: files('win_dump.c'))
