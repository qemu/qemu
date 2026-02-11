arm_system_ss.add(when: 'CONFIG_WHPX', if_true: files(
  'whpx-all.c',
))

arm_common_system_ss.add(when: 'CONFIG_WHPX', if_false: files('whpx-stub.c'))
