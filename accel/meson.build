specific_ss.add(files('accel-target.c'))
system_ss.add(files('accel-system.c', 'accel-blocker.c'))
user_ss.add(files('accel-user.c'))

subdir('tcg')
if have_system
  subdir('hvf')
  subdir('qtest')
  subdir('kvm')
  subdir('xen')
  subdir('stubs')
endif

# qtest
system_ss.add(files('dummy-cpus.c'))
