i3c_ss = ss.source_set()
i3c_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files('aspeed_i3c.c'))
system_ss.add_all(when: 'CONFIG_I3C', if_true: i3c_ss)
