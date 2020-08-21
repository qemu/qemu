authz_ss = ss.source_set()
authz_ss.add(genh)
authz_ss.add(files(
  'base.c',
  'list.c',
  'listfile.c',
  'simple.c',
))

authz_ss.add(when: ['CONFIG_AUTH_PAM', pam], if_true: files('pamacct.c'))

authz_ss = authz_ss.apply(config_host, strict: false)
libauthz = static_library('authz', authz_ss.sources() + genh,
                          dependencies: [authz_ss.dependencies()],
                          name_suffix: 'fa',
                          build_by_default: false)

authz = declare_dependency(link_whole: libauthz,
                           dependencies: qom)
