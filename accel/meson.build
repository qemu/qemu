softmmu_ss.add(files('accel.c'))

subdir('qtest')
subdir('kvm')
subdir('tcg')
subdir('xen')
subdir('stubs')

dummy_ss = ss.source_set()
dummy_ss.add(files(
  'dummy-cpus.c',
))

specific_ss.add_all(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: dummy_ss)
specific_ss.add_all(when: ['CONFIG_XEN'], if_true: dummy_ss)
