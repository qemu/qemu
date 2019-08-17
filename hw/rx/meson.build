rx_ss = ss.source_set()
rx_ss.add(when: 'CONFIG_RX_GDBSIM', if_true: files('rx-gdbsim.c'))
rx_ss.add(when: 'CONFIG_RX62N_MCU', if_true: files('rx62n.c'))

hw_arch += {'rx': rx_ss}
