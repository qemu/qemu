if not get_option('tcg').allowed()
   subdir_done()
endif

tcg_ss = ss.source_set()

tcg_ss.add(files(
  'optimize.c',
  'region.c',
  'tcg.c',
  'tcg-common.c',
  'tcg-op.c',
  'tcg-op-ldst.c',
  'tcg-op-gvec.c',
  'tcg-op-vec.c',
))

if get_option('tcg_interpreter')
  libffi = dependency('libffi', version: '>=3.0', required: true,
                      method: 'pkg-config')
  tcg_ss.add(libffi)
  tcg_ss.add(files('tci.c'))
endif

tcg_ss = tcg_ss.apply(config_host, strict: false)

libtcg_user = static_library('tcg_user',
                             tcg_ss.sources() + genh,
                             name_suffix: 'fa',
                             c_args: '-DCONFIG_USER_ONLY',
                             build_by_default: have_user)

tcg_user = declare_dependency(link_with: libtcg_user,
                              dependencies: tcg_ss.dependencies())
user_ss.add(tcg_user)

libtcg_softmmu = static_library('tcg_softmmu',
                                tcg_ss.sources() + genh,
                                name_suffix: 'fa',
                                c_args: '-DCONFIG_SOFTMMU',
                                build_by_default: have_system)

tcg_softmmu = declare_dependency(link_with: libtcg_softmmu,
                                 dependencies: tcg_ss.dependencies())
system_ss.add(tcg_softmmu)
