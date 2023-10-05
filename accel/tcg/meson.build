tcg_ss = ss.source_set()
common_ss.add(when: 'CONFIG_TCG', if_true: files(
  'cpu-exec-common.c',
))
tcg_ss.add(files(
  'tcg-all.c',
  'cpu-exec.c',
  'tb-maint.c',
  'tcg-runtime-gvec.c',
  'tcg-runtime.c',
  'translate-all.c',
  'translator.c',
))
tcg_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-exec.c'))
tcg_ss.add(when: 'CONFIG_SYSTEM_ONLY', if_false: files('user-exec-stub.c'))
if get_option('plugins')
  tcg_ss.add(files('plugin-gen.c'))
endif
tcg_ss.add(when: libdw, if_true: files('debuginfo.c'))
tcg_ss.add(when: 'CONFIG_LINUX', if_true: files('perf.c'))
specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_ss)

specific_ss.add(when: ['CONFIG_SYSTEM_ONLY', 'CONFIG_TCG'], if_true: files(
  'cputlb.c',
))

system_ss.add(when: ['CONFIG_TCG'], if_true: files(
  'icount-common.c',
  'monitor.c',
))

tcg_module_ss.add(when: ['CONFIG_SYSTEM_ONLY', 'CONFIG_TCG'], if_true: files(
  'tcg-accel-ops.c',
  'tcg-accel-ops-mttcg.c',
  'tcg-accel-ops-icount.c',
  'tcg-accel-ops-rr.c',
))
