softmmu_ss.add(files('accel.c'))
specific_ss.add(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: files('qtest.c'))

subdir('kvm')
subdir('tcg')
subdir('xen')
subdir('stubs')
