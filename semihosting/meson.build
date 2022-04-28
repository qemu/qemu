specific_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'guestfd.c',
  'syscalls.c',
))

specific_ss.add(when: ['CONFIG_SEMIHOSTING', 'CONFIG_SOFTMMU'], if_true: files(
  'config.c',
  'console.c',
  'uaccess.c',
))

specific_ss.add(when: ['CONFIG_ARM_COMPATIBLE_SEMIHOSTING'],
		if_true: files('arm-compat-semi.c'))
