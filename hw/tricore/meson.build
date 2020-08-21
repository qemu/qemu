tricore_ss = ss.source_set()
tricore_ss.add(when: 'CONFIG_TRICORE', if_true: files('tricore_testboard.c'))

hw_arch += {'tricore': tricore_ss}
