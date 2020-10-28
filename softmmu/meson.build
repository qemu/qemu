specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: [files(
  'arch_init.c',
  'balloon.c',
  'cpus.c',
  'cpu-throttle.c',
  'datadir.c',
  'globals.c',
  'physmem.c',
  'ioport.c',
  'rtc.c',
  'runstate.c',
  'memory.c',
  'memory_mapping.c',
  'qtest.c',
  'vl.c',
  'cpu-timers.c',
)])

specific_ss.add(when: ['CONFIG_SOFTMMU', 'CONFIG_TCG'], if_true: [files(
  'icount.c'
)])

softmmu_ss.add(files(
  'bootdevice.c',
  'dma-helpers.c',
  'qdev-monitor.c',
), sdl, libpmem, libdaxctl)

softmmu_ss.add(when: 'CONFIG_TPM', if_true: files('tpm.c'))
softmmu_ss.add(when: 'CONFIG_SECCOMP', if_true: [files('qemu-seccomp.c'), seccomp])
softmmu_ss.add(when: fdt, if_true: files('device_tree.c'))
