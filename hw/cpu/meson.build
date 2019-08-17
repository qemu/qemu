softmmu_ss.add(files('core.c', 'cluster.c'))

specific_ss.add(when: 'CONFIG_ARM11MPCORE', if_true: files('arm11mpcore.c'))
specific_ss.add(when: 'CONFIG_REALVIEW', if_true: files('realview_mpcore.c'))
specific_ss.add(when: 'CONFIG_A9MPCORE', if_true: files('a9mpcore.c'))
specific_ss.add(when: 'CONFIG_A15MPCORE', if_true: files('a15mpcore.c'))
