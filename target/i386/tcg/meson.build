i386_ss.add(when: 'CONFIG_TCG', if_true: files(
  'bpt_helper.c',
  'cc_helper.c',
  'excp_helper.c',
  'fpu_helper.c',
  'int_helper.c',
  'mem_helper.c',
  'misc_helper.c',
  'mpx_helper.c',
  'seg_helper.c',
  'smm_helper.c',
  'svm_helper.c',
  'tcg-cpu.c',
  'translate.c'), if_false: files('tcg-stub.c'))
