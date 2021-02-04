kvm_ss = ss.source_set()
kvm_ss.add(files(
  'kvm-all.c',
  'kvm-accel-ops.c',
))
kvm_ss.add(when: 'CONFIG_SEV', if_false: files('sev-stub.c'))

specific_ss.add_all(when: 'CONFIG_KVM', if_true: kvm_ss)
