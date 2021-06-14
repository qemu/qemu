tcg_ss = ss.source_set()

tcg_ss.add(files(
  'optimize.c',
  'region.c',
  'tcg.c',
  'tcg-common.c',
  'tcg-op.c',
  'tcg-op-gvec.c',
  'tcg-op-vec.c',
))
tcg_ss.add(when: 'CONFIG_TCG_INTERPRETER', if_true: files('tci.c'))

specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_ss)
