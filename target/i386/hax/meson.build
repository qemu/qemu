i386_softmmu_ss.add(when: 'CONFIG_HAX', if_true: files(
  'hax-all.c',
  'hax-mem.c',
  'hax-accel-ops.c',
))
i386_softmmu_ss.add(when: ['CONFIG_HAX', 'CONFIG_POSIX'], if_true: files('hax-posix.c'))
i386_softmmu_ss.add(when: ['CONFIG_HAX', 'CONFIG_WIN32'], if_true: files('hax-windows.c'))
