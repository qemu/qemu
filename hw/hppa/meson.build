hppa_ss = ss.source_set()
hppa_ss.add(when: 'CONFIG_HPPA_B160L', if_true: files('pci.c', 'machine.c', 'lasi.c'))

hw_arch += {'hppa': hppa_ss}
