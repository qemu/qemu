sparc_ss = ss.source_set()
sparc_ss.add(when: 'CONFIG_LEON3', if_true: files('leon3.c'))
sparc_ss.add(when: 'CONFIG_SUN4M', if_true: files('sun4m.c'))
sparc_ss.add(when: 'CONFIG_SUN4M', if_true: files('sun4m_iommu.c'))

hw_arch += {'sparc': sparc_ss}
