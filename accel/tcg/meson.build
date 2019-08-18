tcg_ss = ss.source_set()
tcg_ss.add(files(
  'cpu-exec-common.c',
  'cpu-exec.c',
  'tcg-runtime-gvec.c',
  'tcg-runtime.c',
  'translate-all.c',
  'translator.c',
))
tcg_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-exec.c'))
tcg_ss.add(when: 'CONFIG_SOFTMMU', if_false: files('user-exec-stub.c'))
tcg_ss.add(when: 'CONFIG_PLUGIN', if_true: files('plugin-gen.c'))
specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_ss)

specific_ss.add(when: ['CONFIG_SOFTMMU', 'CONFIG_TCG'], if_true: files('tcg-all.c', 'cputlb.c'))
