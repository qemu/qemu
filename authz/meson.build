authz_ss.add(genh)
authz_ss.add(files(
  'base.c',
  'list.c',
  'listfile.c',
  'simple.c',
))

authz_ss.add(when: pam, if_true: files('pamacct.c'))
