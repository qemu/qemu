specific_ss.add(when: 'CONFIG_SEMIHOSTING', if_true: files(
  'config.c',
  'console.c',
))

specific_ss.add(when: ['CONFIG_ARM_COMPATIBLE_SEMIHOSTING'],
		if_true: files('arm-compat-semi.c'))
