hppa_ss = ss.source_set()
hppa_ss.add(when: 'CONFIG_DINO', if_true: files('pci.c', 'machine.c', 'dino.c', 'lasi.c'))

hw_arch += {'hppa': hppa_ss}
