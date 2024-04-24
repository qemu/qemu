specific_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'guestfd.c',
  'syscalls.c',
))

specific_ss.add(when: ['CONFIG_SEMIHOSTING', 'CONFIG_SYSTEM_ONLY'], if_true: files(
  'config.c',
  'console.c',
  'uaccess.c',
))

common_ss.add(when: ['CONFIG_SEMIHOSTING', 'CONFIG_SYSTEM_ONLY'], if_false: files('stubs-all.c'))
system_ss.add(when: ['CONFIG_SEMIHOSTING'], if_false: files('stubs-system.c'))

specific_ss.add(when: ['CONFIG_ARM_COMPATIBLE_SEMIHOSTING'],
		if_true: files('arm-compat-semi.c'))
