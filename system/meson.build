specific_ss.add(when: 'CONFIG_SYSTEM_ONLY', if_true: [files(
  'arch_init.c',
  'globals-target.c',
)])

system_ss.add(files(
  'vl.c',
), sdl, libpmem, libdaxctl)

libsystem_ss.add(files(
  'balloon.c',
  'bootdevice.c',
  'cpus.c',
  'cpu-timers.c',
  'datadir.c',
  'dirtylimit.c',
  'dma-helpers.c',
  'globals.c',
  'ioport.c',
  'memory_mapping.c',
  'memory.c',
  'physmem.c',
  'qdev-monitor.c',
  'qtest.c',
  'rtc.c',
  'runstate-action.c',
  'runstate-hmp-cmds.c',
  'runstate.c',
  'tpm-hmp-cmds.c',
  'watchpoint.c',
))

if have_tpm
  system_ss.add(files('tpm.c'))
endif

system_ss.add(when: seccomp, if_true: files('qemu-seccomp.c'))
system_ss.add(when: 'CONFIG_DEVICE_TREE',
              if_true: [fdt, files('device_tree.c')],
              if_false: files('device_tree-stub.c'))
if host_os == 'linux'
  system_ss.add(files('async-teardown.c'))
endif
