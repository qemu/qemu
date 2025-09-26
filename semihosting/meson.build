common_ss.add(when: 'CONFIG_SEMIHOSTING', if_false: files('stubs-all.c'))
user_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'user.c',
  'guestfd.c'))
system_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'config.c',
  'console.c',
  'guestfd.c',
  'uaccess.c',
  'syscalls.c',
), if_false: files(
  'stubs-system.c',
))
system_ss.add(when: 'CONFIG_ARM_COMPATIBLE_SEMIHOSTING',
  if_true: files('arm-compat-semi.c'),
  if_false: files('arm-compat-semi-stub.c'))

specific_ss.add(when: ['CONFIG_SEMIHOSTING', 'CONFIG_USER_ONLY'],
                if_true: files('syscalls.c'))
specific_ss.add(when: ['CONFIG_ARM_COMPATIBLE_SEMIHOSTING', 'CONFIG_USER_ONLY'],
		if_true: files('arm-compat-semi.c'))
