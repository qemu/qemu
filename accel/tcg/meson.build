if not get_option('tcg').allowed()
   subdir_done()
endif

tcg_ss = ss.source_set()

tcg_ss.add(files(
  'cpu-exec-common.c',
  'tcg-runtime.c',
  'tcg-runtime-gvec.c',
  'translator.c',
))
if get_option('plugins')
  tcg_ss.add(files('plugin-gen.c'))
endif

libuser_ss.add_all(tcg_ss)
libsystem_ss.add_all(tcg_ss)

tcg_specific_ss = ss.source_set()
tcg_specific_ss.add(files(
  'tcg-all.c',
  'cpu-exec.c',
  'tb-maint.c',
  'translate-all.c',
))
tcg_specific_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-exec.c'))
specific_ss.add_all(when: 'CONFIG_TCG', if_true: tcg_specific_ss)

specific_ss.add(when: ['CONFIG_SYSTEM_ONLY', 'CONFIG_TCG'], if_true: files(
  'cputlb.c',
))

libuser_ss.add(files(
  'user-exec-stub.c',
))

libsystem_ss.add(files(
  'icount-common.c',
  'monitor.c',
  'tcg-accel-ops.c',
  'tcg-accel-ops-icount.c',
  'tcg-accel-ops-mttcg.c',
  'tcg-accel-ops-rr.c',
  'watchpoint.c',
))
