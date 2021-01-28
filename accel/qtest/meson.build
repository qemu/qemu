qtest_ss = ss.source_set()
qtest_ss.add(files(
  'qtest.c',
))

specific_ss.add_all(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: qtest_ss)
