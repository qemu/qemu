softmmu_ss.add(when: 'CONFIG_APPLESMC', if_true: files('applesmc.c'))
softmmu_ss.add(when: 'CONFIG_EDU', if_true: files('edu.c'))
softmmu_ss.add(when: 'CONFIG_FW_CFG_DMA', if_true: files('vmcoreinfo.c'))
softmmu_ss.add(when: 'CONFIG_ISA_DEBUG', if_true: files('debugexit.c'))
softmmu_ss.add(when: 'CONFIG_ISA_TESTDEV', if_true: files('pc-testdev.c'))
softmmu_ss.add(when: 'CONFIG_PCA9552', if_true: files('pca9552.c'))
softmmu_ss.add(when: 'CONFIG_PCI_TESTDEV', if_true: files('pci-testdev.c'))
softmmu_ss.add(when: 'CONFIG_SGA', if_true: files('sga.c'))
softmmu_ss.add(when: 'CONFIG_UNIMP', if_true: files('unimp.c'))
softmmu_ss.add(when: 'CONFIG_EMPTY_SLOT', if_true: files('empty_slot.c'))
softmmu_ss.add(when: 'CONFIG_LED', if_true: files('led.c'))
softmmu_ss.add(when: 'CONFIG_PVPANIC_COMMON', if_true: files('pvpanic.c'))

# ARM devices
softmmu_ss.add(when: 'CONFIG_PL310', if_true: files('arm_l2x0.c'))
softmmu_ss.add(when: 'CONFIG_INTEGRATOR_DEBUG', if_true: files('arm_integrator_debug.c'))
softmmu_ss.add(when: 'CONFIG_A9SCU', if_true: files('a9scu.c'))
softmmu_ss.add(when: 'CONFIG_ARM11SCU', if_true: files('arm11scu.c'))

softmmu_ss.add(when: 'CONFIG_ARM_V7M', if_true: files('armv7m_ras.c'))

# Mac devices
softmmu_ss.add(when: 'CONFIG_MOS6522', if_true: files('mos6522.c'))

# virt devices
softmmu_ss.add(when: 'CONFIG_VIRT_CTRL', if_true: files('virt_ctrl.c'))

# RISC-V devices
softmmu_ss.add(when: 'CONFIG_MCHP_PFSOC_DMC', if_true: files('mchp_pfsoc_dmc.c'))
softmmu_ss.add(when: 'CONFIG_MCHP_PFSOC_IOSCB', if_true: files('mchp_pfsoc_ioscb.c'))
softmmu_ss.add(when: 'CONFIG_MCHP_PFSOC_SYSREG', if_true: files('mchp_pfsoc_sysreg.c'))
softmmu_ss.add(when: 'CONFIG_SIFIVE_TEST', if_true: files('sifive_test.c'))
softmmu_ss.add(when: 'CONFIG_SIFIVE_E_PRCI', if_true: files('sifive_e_prci.c'))
softmmu_ss.add(when: 'CONFIG_SIFIVE_U_OTP', if_true: files('sifive_u_otp.c'))
softmmu_ss.add(when: 'CONFIG_SIFIVE_U_PRCI', if_true: files('sifive_u_prci.c'))

subdir('macio')

softmmu_ss.add(when: 'CONFIG_IVSHMEM_DEVICE', if_true: files('ivshmem.c'))

softmmu_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3-ccu.c'))
specific_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-cpucfg.c'))
softmmu_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3-dramc.c'))
softmmu_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3-sysctrl.c'))
softmmu_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-sid.c'))
softmmu_ss.add(when: 'CONFIG_REALVIEW', if_true: files('arm_sysctl.c'))
softmmu_ss.add(when: 'CONFIG_NSERIES', if_true: files('cbus.c'))
softmmu_ss.add(when: 'CONFIG_ECCMEMCTL', if_true: files('eccmemctl.c'))
softmmu_ss.add(when: 'CONFIG_EXYNOS4', if_true: files('exynos4210_pmu.c', 'exynos4210_clk.c', 'exynos4210_rng.c'))
softmmu_ss.add(when: 'CONFIG_IMX', if_true: files(
  'imx25_ccm.c',
  'imx31_ccm.c',
  'imx6_ccm.c',
  'imx6ul_ccm.c',
  'imx7_ccm.c',
  'imx7_gpr.c',
  'imx7_snvs.c',
  'imx_ccm.c',
  'imx_rngc.c',
))
softmmu_ss.add(when: 'CONFIG_MAINSTONE', if_true: files('mst_fpga.c'))
softmmu_ss.add(when: 'CONFIG_NPCM7XX', if_true: files(
  'npcm7xx_clk.c',
  'npcm7xx_gcr.c',
  'npcm7xx_mft.c',
  'npcm7xx_pwm.c',
  'npcm7xx_rng.c',
))
softmmu_ss.add(when: 'CONFIG_OMAP', if_true: files(
  'omap_clk.c',
  'omap_gpmc.c',
  'omap_l4.c',
  'omap_sdrc.c',
  'omap_tap.c',
))
softmmu_ss.add(when: 'CONFIG_RASPI', if_true: files(
  'bcm2835_mbox.c',
  'bcm2835_mphi.c',
  'bcm2835_property.c',
  'bcm2835_rng.c',
  'bcm2835_thermal.c',
  'bcm2835_cprman.c',
  'bcm2835_powermgt.c',
))
softmmu_ss.add(when: 'CONFIG_SLAVIO', if_true: files('slavio_misc.c'))
softmmu_ss.add(when: 'CONFIG_ZYNQ', if_true: files('zynq_slcr.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_VERSAL', if_true: files('xlnx-versal-xramc.c'))
softmmu_ss.add(when: 'CONFIG_STM32F2XX_SYSCFG', if_true: files('stm32f2xx_syscfg.c'))
softmmu_ss.add(when: 'CONFIG_STM32F4XX_SYSCFG', if_true: files('stm32f4xx_syscfg.c'))
softmmu_ss.add(when: 'CONFIG_STM32F4XX_EXTI', if_true: files('stm32f4xx_exti.c'))
softmmu_ss.add(when: 'CONFIG_MPS2_FPGAIO', if_true: files('mps2-fpgaio.c'))
softmmu_ss.add(when: 'CONFIG_MPS2_SCC', if_true: files('mps2-scc.c'))

softmmu_ss.add(when: 'CONFIG_TZ_MPC', if_true: files('tz-mpc.c'))
softmmu_ss.add(when: 'CONFIG_TZ_MSC', if_true: files('tz-msc.c'))
softmmu_ss.add(when: 'CONFIG_TZ_PPC', if_true: files('tz-ppc.c'))
softmmu_ss.add(when: 'CONFIG_IOTKIT_SECCTL', if_true: files('iotkit-secctl.c'))
softmmu_ss.add(when: 'CONFIG_IOTKIT_SYSINFO', if_true: files('iotkit-sysinfo.c'))
softmmu_ss.add(when: 'CONFIG_ARMSSE_CPU_PWRCTRL', if_true: files('armsse-cpu-pwrctrl.c'))
softmmu_ss.add(when: 'CONFIG_ARMSSE_CPUID', if_true: files('armsse-cpuid.c'))
softmmu_ss.add(when: 'CONFIG_ARMSSE_MHU', if_true: files('armsse-mhu.c'))

softmmu_ss.add(when: 'CONFIG_PVPANIC_ISA', if_true: files('pvpanic-isa.c'))
softmmu_ss.add(when: 'CONFIG_PVPANIC_PCI', if_true: files('pvpanic-pci.c'))
softmmu_ss.add(when: 'CONFIG_AUX', if_true: files('auxbus.c'))
softmmu_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files(
  'aspeed_hace.c',
  'aspeed_lpc.c',
  'aspeed_scu.c',
  'aspeed_sdmc.c',
  'aspeed_xdma.c'))

softmmu_ss.add(when: 'CONFIG_MSF2', if_true: files('msf2-sysreg.c'))
softmmu_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_rng.c'))

softmmu_ss.add(when: 'CONFIG_GRLIB', if_true: files('grlib_ahb_apb_pnp.c'))

specific_ss.add(when: 'CONFIG_AVR_POWER', if_true: files('avr_power.c'))

specific_ss.add(when: 'CONFIG_IMX', if_true: files('imx6_src.c'))
specific_ss.add(when: 'CONFIG_IOTKIT_SYSCTL', if_true: files('iotkit-sysctl.c'))

specific_ss.add(when: 'CONFIG_MAC_VIA', if_true: files('mac_via.c'))

specific_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('mips_cmgcr.c', 'mips_cpc.c'))
specific_ss.add(when: 'CONFIG_MIPS_ITU', if_true: files('mips_itu.c'))

specific_ss.add(when: 'CONFIG_SBSA_REF', if_true: files('sbsa_ec.c'))
