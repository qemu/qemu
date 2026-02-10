gen = decodetree.process('insns.decode')

openrisc_ss = ss.source_set()
openrisc_ss.add(gen)
openrisc_ss.add(files(
  'cpu.c',
  'disas.c',
  'exception.c',
  'exception_helper.c',
  'fpu_helper.c',
  'gdbstub.c',
  'interrupt_helper.c',
  'sys_helper.c',
  'translate.c',
))

openrisc_system_ss = ss.source_set()
openrisc_system_ss.add(files(
  'interrupt.c',
  'machine.c',
  'mmu.c',
))

target_arch += {'or1k': openrisc_ss}
target_common_system_arch += {'or1k': openrisc_system_ss}
