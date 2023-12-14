
s390x_ss.add(when: 'CONFIG_KVM', if_true: files(
  'pv.c',
  'kvm.c',
  'stsi-topology.c'
), if_false: files(
  'stubs.c'
))

# Newer kernels on s390 check for an S390_PGSTE program header and
# enable the pgste page table extensions in that case. This makes
# the vm.allocate_pgste sysctl unnecessary. We enable this program
# header if
#  - we build on s390x
#  - we build the system emulation for s390x (qemu-system-s390x)
#  - KVM is enabled
#  - the linker supports --s390-pgste
if host_machine.cpu_family() == 's390x' and cc.has_link_argument('-Wl,--s390-pgste')
  s390x_system_ss.add(when: 'CONFIG_KVM',
                       if_true: declare_dependency(link_args: ['-Wl,--s390-pgste']))
endif
