common_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cpu-exec-common.c',
  'tcg-runtime.c',
  'tcg-runtime-gvec.c',
))
tcg_specific_ss = ss.source_set()
tcg_specific_ss.add(files(
  'tcg-all.c',
  'cpu-exec.c',
  'tb-maint.c',
  'translate-all.c',
  'translator.c',
))
tcg_specific_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-exec.c'))
tcg_specific_ss.add(when: 'CONFIG_SYSTEM_ONLY', if_false: files('user-exec-stub.c'))
if get_option('plugins')
  tcg_specific_ss.add(files('plugin-gen.c'))
endif
specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_specific_ss)

specific_ss.add(when: ['CONFIG_SYSTEM_ONLY', 'CONFIG_TCG'], if_true: files(
  'cputlb.c',
))

system_ss.add(when: ['CONFIG_TCG'], if_true: files(
  'icount-common.c',
  'monitor.c',
  'tcg-accel-ops.c',
  'tcg-accel-ops-icount.c',
  'tcg-accel-ops-mttcg.c',
  'tcg-accel-ops-rr.c',
  'watchpoint.c',
))
