softmmu_ss.add(files('intc.c'))
softmmu_ss.add(when: 'CONFIG_ARM_GIC', if_true: files(
  'arm_gic.c',
  'arm_gic_common.c',
  'arm_gicv2m.c',
  'arm_gicv3.c',
  'arm_gicv3_common.c',
  'arm_gicv3_dist.c',
  'arm_gicv3_its_common.c',
  'arm_gicv3_redist.c',
))
softmmu_ss.add(when: 'CONFIG_ETRAXFS', if_true: files('etraxfs_pic.c'))
softmmu_ss.add(when: 'CONFIG_HEATHROW_PIC', if_true: files('heathrow_pic.c'))
softmmu_ss.add(when: 'CONFIG_I8259', if_true: files('i8259_common.c', 'i8259.c'))
softmmu_ss.add(when: 'CONFIG_IMX', if_true: files('imx_avic.c', 'imx_gpcv2.c'))
softmmu_ss.add(when: 'CONFIG_IOAPIC', if_true: files('ioapic_common.c'))
softmmu_ss.add(when: 'CONFIG_LM32', if_true: files('lm32_pic.c'))
softmmu_ss.add(when: 'CONFIG_OPENPIC', if_true: files('openpic.c'))
softmmu_ss.add(when: 'CONFIG_PL190', if_true: files('pl190.c'))
softmmu_ss.add(when: 'CONFIG_PUV3', if_true: files('puv3_intc.c'))
softmmu_ss.add(when: 'CONFIG_REALVIEW', if_true: files('realview_gic.c'))
softmmu_ss.add(when: 'CONFIG_SLAVIO', if_true: files('slavio_intctl.c'))
softmmu_ss.add(when: 'CONFIG_XILINX', if_true: files('xilinx_intc.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_ZYNQMP', if_true: files('xlnx-zynqmp-ipi.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_ZYNQMP_PMU', if_true: files('xlnx-pmu-iomod-intc.c'))

specific_ss.add(when: 'CONFIG_ALLWINNER_A10_PIC', if_true: files('allwinner-a10-pic.c'))
specific_ss.add(when: 'CONFIG_APIC', if_true: files('apic.c', 'apic_common.c'))
specific_ss.add(when: 'CONFIG_ARM_GIC', if_true: files('arm_gicv3_cpuif.c'))
specific_ss.add(when: 'CONFIG_ARM_GIC_KVM', if_true: files('arm_gic_kvm.c'))
specific_ss.add(when: ['CONFIG_ARM_GIC_KVM', 'TARGET_AARCH64'], if_true: files('arm_gicv3_kvm.c', 'arm_gicv3_its_kvm.c'))
specific_ss.add(when: 'CONFIG_ARM_V7M', if_true: files('armv7m_nvic.c'))
specific_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files('aspeed_vic.c'))
specific_ss.add(when: 'CONFIG_EXYNOS4', if_true: files('exynos4210_gic.c', 'exynos4210_combiner.c'))
specific_ss.add(when: 'CONFIG_GRLIB', if_true: files('grlib_irqmp.c'))
specific_ss.add(when: 'CONFIG_IBEX', if_true: files('ibex_plic.c'))
specific_ss.add(when: 'CONFIG_IOAPIC', if_true: files('ioapic.c'))
specific_ss.add(when: 'CONFIG_LOONGSON_LIOINTC', if_true: files('loongson_liointc.c'))
specific_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('mips_gic.c'))
specific_ss.add(when: 'CONFIG_OMAP', if_true: files('omap_intc.c'))
specific_ss.add(when: 'CONFIG_OMPIC', if_true: files('ompic.c'))
specific_ss.add(when: ['CONFIG_KVM', 'CONFIG_OPENPIC'],
		if_true: files('openpic_kvm.c'))
specific_ss.add(when: 'CONFIG_POWERNV', if_true: files('xics_pnv.c', 'pnv_xive.c'))
specific_ss.add(when: 'CONFIG_PPC_UIC', if_true: files('ppc-uic.c'))
specific_ss.add(when: 'CONFIG_RASPI', if_true: files('bcm2835_ic.c', 'bcm2836_control.c'))
specific_ss.add(when: 'CONFIG_RX_ICU', if_true: files('rx_icu.c'))
specific_ss.add(when: 'CONFIG_S390_FLIC', if_true: files('s390_flic.c'))
specific_ss.add(when: 'CONFIG_S390_FLIC_KVM', if_true: files('s390_flic_kvm.c'))
specific_ss.add(when: 'CONFIG_SH4', if_true: files('sh_intc.c'))
specific_ss.add(when: 'CONFIG_SIFIVE_CLINT', if_true: files('sifive_clint.c'))
specific_ss.add(when: 'CONFIG_SIFIVE_PLIC', if_true: files('sifive_plic.c'))
specific_ss.add(when: 'CONFIG_XICS', if_true: files('xics.c'))
specific_ss.add(when: ['CONFIG_KVM', 'CONFIG_XICS'],
		if_true: files('xics_kvm.c'))
specific_ss.add(when: 'CONFIG_PSERIES', if_true: files('xics_spapr.c', 'spapr_xive.c'))
specific_ss.add(when: 'CONFIG_XIVE', if_true: files('xive.c'))
specific_ss.add(when: ['CONFIG_KVM', 'CONFIG_XIVE'],
		if_true: files('spapr_xive_kvm.c'))
