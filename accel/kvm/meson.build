kvm_ss = ss.source_set()
kvm_ss.add(files(
  'kvm-all.c',
  'kvm-accel-ops.c',
))

specific_ss.add_all(when: 'CONFIG_KVM', if_true: kvm_ss)
