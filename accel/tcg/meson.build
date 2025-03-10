common_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cpu-exec-common.c',
  'tcg-runtime.c',
  'tcg-runtime-gvec.c',
))
if get_option('plugins')
  common_ss.add(when: 'CONFIG_TCG', if_true: files('plugin-gen.c'))
endif

tcg_specific_ss = ss.source_set()
tcg_specific_ss.add(files(
  'tcg-all.c',
  'cpu-exec.c',
  'tb-maint.c',
  'translate-all.c',
  'translator.c',
))
tcg_specific_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-exec.c'))
specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_specific_ss)

specific_ss.add(when: ['CONFIG_SYSTEM_ONLY', 'CONFIG_TCG'], if_true: files(
  'cputlb.c',
))

user_ss.add(when: ['CONFIG_TCG'], if_true: files(
  'user-exec-stub.c',
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
