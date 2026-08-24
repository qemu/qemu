s390x_ss.add(when: 'CONFIG_TCG', if_true: files(
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
))
s390x_common_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cc_helper.c',
  'cpacf_aes.c',
  'cpacf_sha256.c',
  'cpacf_sha512.c',
  'crypto_helper.c',
  'excp_helper.c',
  'fpu_helper.c',
  'translate.c',
  'vec_fpu_helper.c',
  'vec_helper.c',
  'vec_int_helper.c',
  'vec_string_helper.c',
))
s390x_common_system_ss.add(when: 'CONFIG_TCG', if_true: files(
  'debug.c',
))
