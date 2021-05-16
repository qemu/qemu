specific_ss.add(files('accel-common.c'))
softmmu_ss.add(files('accel-softmmu.c'))
user_ss.add(files('accel-user.c'))

subdir('tcg')
if have_system
  subdir('hvf')
  subdir('qtest')
  subdir('kvm')
  subdir('xen')
  subdir('stubs')
endif

dummy_ss = ss.source_set()
dummy_ss.add(files(
  'dummy-cpus.c',
))

specific_ss.add_all(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: dummy_ss)
specific_ss.add_all(when: ['CONFIG_XEN'], if_true: dummy_ss)
