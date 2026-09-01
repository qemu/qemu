system_ss.add([files('dump.c'), snappy, lzo])
if have_hmp
  system_ss.add(files('dump-hmp-cmds.c'))
endif
specific_ss.add(when: 'CONFIG_WINDUMP', if_true: files('win_dump-x86.c'))
system_ss.add(when: 'CONFIG_WINDUMP', if_false: files('win_dump-stubs.c'))
