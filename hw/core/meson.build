# core qdev-related obj files, also used by *-user and unit tests
hwcore_files = files(
  'bus.c',
  'fw-path-provider.c',
  'hotplug.c',
  'qdev-properties.c',
  'qdev.c',
  'reset.c',
  'resettable.c',
  'vmstate-if.c',
  # irq.c needed for qdev GPIO handling:
  'irq.c',
  'clock.c',
  'qdev-clock.c',
)

libhwcore = static_library('hwcore', sources: hwcore_files + genh,
                           name_suffix: 'fa',
                           build_by_default: false)
hwcore = declare_dependency(link_whole: libhwcore)
common_ss.add(hwcore)

common_ss.add(files('cpu.c'))
common_ss.add(when: 'CONFIG_FITLOADER', if_true: files('loader-fit.c'))
common_ss.add(when: 'CONFIG_GENERIC_LOADER', if_true: files('generic-loader.c'))
common_ss.add(when: 'CONFIG_OR_IRQ', if_true: files('or-irq.c'))
common_ss.add(when: 'CONFIG_PLATFORM_BUS', if_true: files('platform-bus.c'))
common_ss.add(when: 'CONFIG_PTIMER', if_true: files('ptimer.c'))
common_ss.add(when: 'CONFIG_REGISTER', if_true: files('register.c'))
common_ss.add(when: 'CONFIG_SPLIT_IRQ', if_true: files('split-irq.c'))
common_ss.add(when: 'CONFIG_XILINX_AXI', if_true: files('stream.c'))

softmmu_ss.add(files(
  'loader.c',
  'machine-hmp-cmds.c',
  'machine.c',
  'nmi.c',
  'null-machine.c',
  'qdev-fw.c',
  'qdev-properties-system.c',
  'sysbus.c',
  'vm-change-state-handler.c',
  'clock-vmstate.c',
))

specific_ss.add(when: 'CONFIG_SOFTMMU', if_true: files(
  'machine-qmp-cmds.c',
  'numa.c',
))
