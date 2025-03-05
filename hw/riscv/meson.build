riscv_ss = ss.source_set()
riscv_ss.add(files('boot.c'))
riscv_ss.add(when: 'CONFIG_RISCV_NUMA', if_true: files('numa.c'))
riscv_ss.add(files('riscv_hart.c'))
riscv_ss.add(when: 'CONFIG_OPENTITAN', if_true: files('opentitan.c'))
riscv_ss.add(when: 'CONFIG_RISCV_VIRT', if_true: files('virt.c'))
riscv_ss.add(when: 'CONFIG_SHAKTI_C', if_true: files('shakti_c.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_E', if_true: files('sifive_e.c'))
riscv_ss.add(when: 'CONFIG_SIFIVE_U', if_true: files('sifive_u.c'))
riscv_ss.add(when: 'CONFIG_SPIKE', if_true: files('spike.c'))
riscv_ss.add(when: 'CONFIG_MICROCHIP_PFSOC', if_true: files('microchip_pfsoc.c'))
riscv_ss.add(when: 'CONFIG_ACPI', if_true: files('virt-acpi-build.c'))
riscv_ss.add(when: 'CONFIG_RISCV_IOMMU', if_true: files(
	'riscv-iommu.c', 'riscv-iommu-pci.c', 'riscv-iommu-sys.c', 'riscv-iommu-hpm.c'))
riscv_ss.add(when: 'CONFIG_MICROBLAZE_V', if_true: files('microblaze-v-generic.c'))

hw_arch += {'riscv': riscv_ss}
