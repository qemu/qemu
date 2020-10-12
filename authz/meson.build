authz_ss.add(genh)
authz_ss.add(files(
  'base.c',
  'list.c',
  'listfile.c',
  'simple.c',
))

authz_ss.add(when: ['CONFIG_AUTH_PAM', pam], if_true: files('pamacct.c'))
