linux_user_ss.add(files(
  'elfload.c',
  'exit.c',
  'fd-trans.c',
  'linuxload.c',
  'main.c',
  'mmap.c',
  'safe-syscall.S',
  'signal.c',
  'strace.c',
  'syscall.c',
  'uaccess.c',
  'uname.c',
))
linux_user_ss.add(rt)

linux_user_ss.add(when: 'TARGET_HAS_BFLT', if_true: files('flatload.c'))
linux_user_ss.add(when: 'TARGET_I386', if_true: files('vm86.c'))
linux_user_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING', if_true: files('semihost.c'))


syscall_nr_generators = {}

subdir('alpha')
subdir('arm')
subdir('hppa')
subdir('i386')
subdir('m68k')
subdir('microblaze')
subdir('mips64')
subdir('mips')
subdir('ppc')
subdir('s390x')
subdir('sh4')
subdir('sparc64')
subdir('sparc')
subdir('x86_64')
subdir('xtensa')
