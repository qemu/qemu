system_ss.add(when: 'CONFIG_APPLESMC', if_true: files('applesmc.c'))
system_ss.add(when: 'CONFIG_EDU', if_true: files('edu.c'))
system_ss.add(when: 'CONFIG_FW_CFG_DMA', if_true: files('vmcoreinfo.c'))
system_ss.add(when: 'CONFIG_ISA_DEBUG', if_true: files('debugexit.c'))
system_ss.add(when: 'CONFIG_ISA_TESTDEV', if_true: files('pc-testdev.c'))
system_ss.add(when: 'CONFIG_PCI_TESTDEV', if_true: files('pci-testdev.c'))
system_ss.add(when: 'CONFIG_UNIMP', if_true: files('unimp.c'))
system_ss.add(when: 'CONFIG_EMPTY_SLOT', if_true: files('empty_slot.c'))
system_ss.add(when: 'CONFIG_LED', if_true: files('led.c'))
system_ss.add(when: 'CONFIG_PVPANIC_COMMON', if_true: files('pvpanic.c'))

# ARM devices
system_ss.add(when: 'CONFIG_PL310', if_true: files('arm_l2x0.c'))
system_ss.add(when: 'CONFIG_INTEGRATOR_DEBUG', if_true: files('arm_integrator_debug.c'))
system_ss.add(when: 'CONFIG_A9SCU', if_true: files('a9scu.c'))
system_ss.add(when: 'CONFIG_ARM11SCU', if_true: files('arm11scu.c'))

system_ss.add(when: 'CONFIG_ARM_V7M', if_true: files('armv7m_ras.c'))

# Mac devices
system_ss.add(when: 'CONFIG_MOS6522', if_true: files('mos6522.c'))
system_ss.add(when: 'CONFIG_DJMEMC', if_true: files('djmemc.c'))
system_ss.add(when: 'CONFIG_IOSB', if_true: files('iosb.c'))

# virt devices
system_ss.add(when: 'CONFIG_VIRT_CTRL', if_true: files('virt_ctrl.c'))

# RISC-V devices
system_ss.add(when: 'CONFIG_MCHP_PFSOC_DMC', if_true: files('mchp_pfsoc_dmc.c'))
system_ss.add(when: 'CONFIG_MCHP_PFSOC_IOSCB', if_true: files('mchp_pfsoc_ioscb.c'))
system_ss.add(when: 'CONFIG_MCHP_PFSOC_SYSREG', if_true: files('mchp_pfsoc_sysreg.c'))
system_ss.add(when: 'CONFIG_SIFIVE_TEST', if_true: files('sifive_test.c'))
system_ss.add(when: 'CONFIG_SIFIVE_E_PRCI', if_true: files('sifive_e_prci.c'))
system_ss.add(when: 'CONFIG_SIFIVE_E_AON', if_true: files('sifive_e_aon.c'))
system_ss.add(when: 'CONFIG_SIFIVE_U_OTP', if_true: files('sifive_u_otp.c'))
system_ss.add(when: 'CONFIG_SIFIVE_U_PRCI', if_true: files('sifive_u_prci.c'))

subdir('macio')

# ivshmem devices
system_ss.add(when: 'CONFIG_IVSHMEM_DEVICE', if_true: files('ivshmem-pci.c'))
system_ss.add(when: 'CONFIG_IVSHMEM_FLAT_DEVICE', if_true: files('ivshmem-flat.c'))

system_ss.add(when: 'CONFIG_ALLWINNER_SRAMC', if_true: files('allwinner-sramc.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_A10_CCM', if_true: files('allwinner-a10-ccm.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_A10_DRAMC', if_true: files('allwinner-a10-dramc.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3-ccu.c'))
specific_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-cpucfg.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3-dramc.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3-sysctrl.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-sid.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_R40', if_true: files('allwinner-r40-ccu.c'))
system_ss.add(when: 'CONFIG_ALLWINNER_R40', if_true: files('allwinner-r40-dramc.c'))
system_ss.add(when: 'CONFIG_AXP2XX_PMU', if_true: files('axp2xx.c'))
system_ss.add(when: 'CONFIG_REALVIEW', if_true: files('arm_sysctl.c'))
system_ss.add(when: 'CONFIG_ECCMEMCTL', if_true: files('eccmemctl.c'))
system_ss.add(when: 'CONFIG_EXYNOS4', if_true: files('exynos4210_pmu.c', 'exynos4210_clk.c', 'exynos4210_rng.c'))
system_ss.add(when: 'CONFIG_FSL_IMX8MP_ANALOG', if_true: files('imx8mp_analog.c'))
system_ss.add(when: 'CONFIG_FSL_IMX8MP_CCM', if_true: files('imx8mp_ccm.c'))
system_ss.add(when: 'CONFIG_IMX', if_true: files(
  'imx25_ccm.c',
  'imx31_ccm.c',
  'imx6_ccm.c',
  'imx6_src.c',
  'imx6ul_ccm.c',
  'imx7_ccm.c',
  'imx7_src.c',
  'imx7_gpr.c',
  'imx7_snvs.c',
  'imx_ccm.c',
  'imx_rngc.c',
))
system_ss.add(when: 'CONFIG_MAX78000_AES', if_true: files('max78000_aes.c'))
system_ss.add(when: 'CONFIG_MAX78000_GCR', if_true: files('max78000_gcr.c'))
system_ss.add(when: 'CONFIG_MAX78000_ICC', if_true: files('max78000_icc.c'))
system_ss.add(when: 'CONFIG_MAX78000_TRNG', if_true: files('max78000_trng.c'))
system_ss.add(when: 'CONFIG_NPCM7XX', if_true: files(
  'npcm_clk.c',
  'npcm_gcr.c',
  'npcm7xx_mft.c',
  'npcm7xx_pwm.c',
  'npcm7xx_rng.c',
))
system_ss.add(when: 'CONFIG_OMAP', if_true: files(
  'omap_clk.c',
))
system_ss.add(when: 'CONFIG_RASPI', if_true: files(
  'bcm2835_mbox.c',
  'bcm2835_mphi.c',
  'bcm2835_property.c',
  'bcm2835_rng.c',
  'bcm2835_thermal.c',
  'bcm2835_cprman.c',
  'bcm2835_powermgt.c',
))
system_ss.add(when: 'CONFIG_SLAVIO', if_true: files('slavio_misc.c'))
system_ss.add(when: 'CONFIG_ZYNQ', if_true: files('zynq_slcr.c'))
system_ss.add(when: 'CONFIG_XLNX_ZYNQMP_ARM', if_true: files('xlnx-zynqmp-crf.c'))
system_ss.add(when: 'CONFIG_XLNX_ZYNQMP_ARM', if_true: files('xlnx-zynqmp-apu-ctrl.c'))
system_ss.add(when: 'CONFIG_XLNX_VERSAL', if_true: files(
  'xlnx-versal-crl.c',
  'xlnx-versal-xramc.c',
  'xlnx-versal-pmc-iou-slcr.c',
  'xlnx-versal-cfu.c',
  'xlnx-cfi-if.c',
  'xlnx-versal-cframe-reg.c',
))
system_ss.add(when: 'CONFIG_XLNX_VERSAL_TRNG', if_true: files(
  'xlnx-versal-trng.c',
))
system_ss.add(when: 'CONFIG_STM32_RCC', if_true: files('stm32_rcc.c'))
system_ss.add(when: 'CONFIG_STM32F2XX_SYSCFG', if_true: files('stm32f2xx_syscfg.c'))
system_ss.add(when: 'CONFIG_STM32F4XX_SYSCFG', if_true: files('stm32f4xx_syscfg.c'))
system_ss.add(when: 'CONFIG_STM32F4XX_EXTI', if_true: files('stm32f4xx_exti.c'))
system_ss.add(when: 'CONFIG_STM32L4X5_EXTI', if_true: files('stm32l4x5_exti.c'))
system_ss.add(when: 'CONFIG_STM32L4X5_SYSCFG', if_true: files('stm32l4x5_syscfg.c'))
system_ss.add(when: 'CONFIG_STM32L4X5_RCC', if_true: files('stm32l4x5_rcc.c'))
system_ss.add(when: 'CONFIG_MPS2_FPGAIO', if_true: files('mps2-fpgaio.c'))
system_ss.add(when: 'CONFIG_MPS2_SCC', if_true: files('mps2-scc.c'))

system_ss.add(when: 'CONFIG_TZ_MPC', if_true: files('tz-mpc.c'))
system_ss.add(when: 'CONFIG_TZ_MSC', if_true: files('tz-msc.c'))
system_ss.add(when: 'CONFIG_TZ_PPC', if_true: files('tz-ppc.c'))
system_ss.add(when: 'CONFIG_IOTKIT_SECCTL', if_true: files('iotkit-secctl.c'))
system_ss.add(when: 'CONFIG_IOTKIT_SYSCTL', if_true: files('iotkit-sysctl.c'))
system_ss.add(when: 'CONFIG_IOTKIT_SYSINFO', if_true: files('iotkit-sysinfo.c'))
system_ss.add(when: 'CONFIG_ARMSSE_CPU_PWRCTRL', if_true: files('armsse-cpu-pwrctrl.c'))
system_ss.add(when: 'CONFIG_ARMSSE_CPUID', if_true: files('armsse-cpuid.c'))
system_ss.add(when: 'CONFIG_ARMSSE_MHU', if_true: files('armsse-mhu.c'))

system_ss.add(when: 'CONFIG_PVPANIC_ISA', if_true: files('pvpanic-isa.c'))
system_ss.add(when: 'CONFIG_PVPANIC_PCI', if_true: files('pvpanic-pci.c'))
system_ss.add(when: 'CONFIG_PVPANIC_MMIO', if_true: files('pvpanic-mmio.c'))
system_ss.add(when: 'CONFIG_AUX', if_true: files('auxbus.c'))
system_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files(
  'aspeed_hace.c',
  'aspeed_i3c.c',
  'aspeed_lpc.c',
  'aspeed_scu.c',
  'aspeed_sbc.c',
  'aspeed_sdmc.c',
  'aspeed_xdma.c',
  'aspeed_peci.c',
  'aspeed_sli.c'))

system_ss.add(when: 'CONFIG_MSF2', if_true: files('msf2-sysreg.c'))
system_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_rng.c'))

system_ss.add(when: 'CONFIG_GRLIB', if_true: files('grlib_ahb_apb_pnp.c'))

system_ss.add(when: 'CONFIG_I2C_ECHO', if_true: files('i2c-echo.c'))

specific_ss.add(when: 'CONFIG_AVR_POWER', if_true: files('avr_power.c'))

specific_ss.add(when: 'CONFIG_MAC_VIA', if_true: files('mac_via.c'))

specific_ss.add(when: 'CONFIG_MIPS_CPS', if_true: files('mips_cmgcr.c', 'mips_cpc.c'))
specific_ss.add(when: 'CONFIG_MIPS_ITU', if_true: files('mips_itu.c'))

system_ss.add(when: 'CONFIG_SBSA_REF', if_true: files('sbsa_ec.c'))

# HPPA devices
system_ss.add(when: 'CONFIG_LASI', if_true: files('lasi.c'))
