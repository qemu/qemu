sparc64_ss = ss.source_set()
sparc64_ss.add(files('sparc64.c'))
sparc64_ss.add(when: 'CONFIG_NIAGARA', if_true: files('niagara.c'))
sparc64_ss.add(when: 'CONFIG_SUN4U', if_true: files('sun4u.c', 'sun4u_iommu.c'))

hw_arch += {'sparc64': sparc64_ss}
