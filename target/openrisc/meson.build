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
  'mmu.c',
  'sys_helper.c',
  'translate.c',
))

openrisc_softmmu_ss = ss.source_set()
openrisc_softmmu_ss.add(files(
  'interrupt.c',
  'machine.c',
))

target_arch += {'openrisc': openrisc_ss}
target_softmmu_arch += {'openrisc': openrisc_softmmu_ss}
