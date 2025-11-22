tricore_ss = ss.source_set()
tricore_ss.add(when: 'CONFIG_TRICORE_TESTBOARD', if_true: files('tricore_testboard.c'))
tricore_ss.add(when: 'CONFIG_TRICORE_TESTBOARD', if_true: files('tricore_testdevice.c'))
tricore_ss.add(when: 'CONFIG_TRIBOARD', if_true: files('triboard.c'))
tricore_ss.add(when: 'CONFIG_TC27X_SOC', if_true: files('tc27x_soc.c'))
tricore_ss.add(when: 'CONFIG_TC39X_SOC', if_true: files('tc39x_soc.c'))
tricore_ss.add(when: 'CONFIG_TC_STM', if_true: files('tc_stm.c'))
tricore_ss.add(when: 'CONFIG_TC_IR', if_true: files('tc_ir.c'))

hw_arch += {'tricore': tricore_ss}
