i386_ss.add(when: 'CONFIG_KVM', if_false: files('kvm-stub.c'))

i386_softmmu_ss.add(when: 'CONFIG_KVM', if_true: files(
  'kvm.c',
  'kvm-cpu.c',
))

i386_softmmu_ss.add(when: 'CONFIG_HYPERV', if_true: files('hyperv.c'), if_false: files('hyperv-stub.c'))
