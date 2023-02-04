specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files(
  'arch_init.c',
  'ioport.c',
  'memory.c',
  'physmem.c',
  'qtest.c',
  'dirtylimit.c',
)])

specific_ss.add(when: ['CONFIG_SOFTMMU', 'CONFIG_TCG'], if_true: [files(
  'icount.c'
)])

softmmu_ss.add(files(
  'balloon.c',
  'bootdevice.c',
  'cpus.c',
  'cpu-throttle.c',
  'cpu-timers.c',
  'datadir.c',
  'dma-helpers.c',
  'globals.c',
  'memory_mapping.c',
  'qdev-monitor.c',
  'rtc.c',
  'runstate-action.c',
  'runstate-hmp-cmds.c',
  'runstate.c',
  'tpm-hmp-cmds.c',
  'vl.c',
), sdl, libpmem, libdaxctl)

if have_tpm
  softmmu_ss.add(files('tpm.c'))
endif

softmmu_ss.add(when: seccomp, if_true: files('qemu-seccomp.c'))
softmmu_ss.add(when: fdt, if_true: files('device_tree.c'))
