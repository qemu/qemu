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
                      method: 'pkg-config', kwargs: static_kwargs)
  specific_ss.add(libffi)
  specific_ss.add(files('tci.c'))
endif

specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_ss)
