i386_kvm_ss = ss.source_set()

i386_kvm_ss.add(files(
  'kvm.c',
  'kvm-cpu.c',
  'vmsr_energy.c',
))

i386_kvm_ss.add(when: 'CONFIG_XEN_EMU', if_true: files('xen-emu.c'))

i386_system_ss.add(when: 'CONFIG_HYPERV', if_true: files('hyperv.c'), if_false: files('hyperv-stub.c'))

i386_system_ss.add_all(when: 'CONFIG_KVM', if_true: i386_kvm_ss)
