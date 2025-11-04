arm_ss = ss.source_set()
arm_common_ss = ss.source_set()
arm_common_ss.add(when: 'CONFIG_ARM_VIRT', if_true: files('virt.c'))
arm_common_ss.add(when: 'CONFIG_ACPI', if_true: files('virt-acpi-build.c'))
arm_common_ss.add(when: 'CONFIG_DIGIC', if_true: files('digic_boards.c'))
arm_common_ss.add(when: 'CONFIG_EMCRAFT_SF2', if_true: files('msf2-som.c'))
arm_common_ss.add(when: 'CONFIG_HIGHBANK', if_true: files('highbank.c'))
arm_common_ss.add(when: 'CONFIG_INTEGRATOR', if_true: files('integratorcp.c'))
arm_common_ss.add(when: 'CONFIG_MICROBIT', if_true: files('microbit.c'))
arm_common_ss.add(when: 'CONFIG_MPS3R', if_true: files('mps3r.c'))
arm_common_ss.add(when: 'CONFIG_MUSICPAL', if_true: [files('musicpal.c')])
arm_common_ss.add(when: 'CONFIG_NETDUINOPLUS2', if_true: files('netduinoplus2.c'))
arm_common_ss.add(when: 'CONFIG_OLIMEX_STM32_H405', if_true: files('olimex-stm32-h405.c'))
arm_common_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx.c', 'npcm7xx_boards.c'))
arm_common_ss.add(when: 'CONFIG_NPCM8XX', if_true: files('npcm8xx.c', 'npcm8xx_boards.c'))
arm_common_ss.add(when: 'CONFIG_REALVIEW', if_true: files('realview.c'))
arm_common_ss.add(when: 'CONFIG_SBSA_REF', if_true: files('sbsa-ref.c'))
arm_common_ss.add(when: 'CONFIG_STELLARIS', if_true: files('stellaris.c'))
arm_common_ss.add(when: 'CONFIG_STM32VLDISCOVERY', if_true: files('stm32vldiscovery.c'))
arm_common_ss.add(when: 'CONFIG_ZYNQ', if_true: files('xilinx_zynq.c'))
arm_common_ss.add(when: 'CONFIG_SABRELITE', if_true: files('sabrelite.c'))

arm_common_ss.add(when: 'CONFIG_ARM_V7M', if_true: files('armv7m.c'))
arm_common_ss.add(when: 'CONFIG_EXYNOS4', if_true: files('exynos4210.c'))
arm_common_ss.add(when: 'CONFIG_DIGIC', if_true: files('digic.c'))
arm_common_ss.add(when: 'CONFIG_OMAP', if_true: files('omap1.c'))
arm_common_ss.add(when: 'CONFIG_ALLWINNER_A10', if_true: files('allwinner-a10.c', 'cubieboard.c'))
arm_common_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-h3.c', 'orangepi.c'))
arm_common_ss.add(when: 'CONFIG_ALLWINNER_R40', if_true: files('allwinner-r40.c', 'bananapi_m2u.c'))
arm_common_ss.add(when: 'CONFIG_MAX78000_SOC', if_true: files('max78000_soc.c'))
arm_ss.add(when: 'CONFIG_RASPI', if_true: files('bcm2836.c', 'raspi.c'))
arm_common_ss.add(when: ['CONFIG_RASPI', 'TARGET_AARCH64'], if_true: files('bcm2838.c', 'raspi4b.c'))
arm_common_ss.add(when: 'CONFIG_STM32F100_SOC', if_true: files('stm32f100_soc.c'))
arm_common_ss.add(when: 'CONFIG_STM32F205_SOC', if_true: files('stm32f205_soc.c'))
arm_common_ss.add(when: 'CONFIG_STM32F405_SOC', if_true: files('stm32f405_soc.c'))
arm_common_ss.add(when: 'CONFIG_B_L475E_IOT01A', if_true: files('b-l475e-iot01a.c'))
arm_common_ss.add(when: 'CONFIG_STM32L4X5_SOC', if_true: files('stm32l4x5_soc.c'))
arm_common_ss.add(when: 'CONFIG_XLNX_ZYNQMP_ARM', if_true: files('xlnx-zynqmp.c', 'xlnx-zcu102.c'))
arm_common_ss.add(when: 'CONFIG_XLNX_VERSAL', if_true: files('xlnx-versal.c', 'xlnx-versal-virt.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX25', if_true: files('fsl-imx25.c', 'imx25_pdk.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX31', if_true: files('fsl-imx31.c', 'kzm.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX6', if_true: files('fsl-imx6.c'))
arm_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files(
  'aspeed.c',
  'aspeed_soc_common.c',
  'aspeed_ast2400.c',
  'aspeed_ast2400_palmetto.c',
  'aspeed_ast2400_quanta-q71l.c',
  'aspeed_ast2400_supermicrox11.c',
  'aspeed_ast2500_evb.c',
  'aspeed_ast2500_fp5280g2.c',
  'aspeed_ast2500_g220a.c',
  'aspeed_ast2500_romulus.c',
  'aspeed_ast2500_sonorapass.c',
  'aspeed_ast2500_supermicro-x11spi.c',
  'aspeed_ast2500_tiogapass.c',
  'aspeed_ast2500_witherspoon.c',
  'aspeed_ast2500_yosemitev2.c',
  'aspeed_ast2600.c',
  'aspeed_ast2600_bletchley.c',
  'aspeed_ast2600_catalina.c',
  'aspeed_ast2600_evb.c',
  'aspeed_ast2600_fby35.c',
  'aspeed_ast2600_fuji.c',
  'aspeed_ast2600_gb200nvl.c',
  'aspeed_ast2600_qcom-dc-scm-v1.c',
  'aspeed_ast2600_qcom-firework.c',
  'aspeed_ast2600_rainier.c',
  'aspeed_ast10x0.c',
  'aspeed_ast10x0_evb.c',
  'fby35.c'))
arm_common_ss.add(when: ['CONFIG_ASPEED_SOC', 'TARGET_AARCH64'], if_true: files(
  'aspeed_ast27x0.c',
  'aspeed_ast27x0_evb.c',
  'aspeed_ast27x0-fc.c',
  'aspeed_ast27x0-ssp.c',
  'aspeed_ast27x0-tsp.c',
  'aspeed_coprocessor_common.c'))
arm_common_ss.add(when: 'CONFIG_MPS2', if_true: files('mps2.c'))
arm_common_ss.add(when: 'CONFIG_MPS2', if_true: files('mps2-tz.c'))
arm_common_ss.add(when: 'CONFIG_MSF2', if_true: files('msf2-soc.c'))
arm_common_ss.add(when: 'CONFIG_MUSCA', if_true: files('musca.c'))
arm_common_ss.add(when: 'CONFIG_ARMSSE', if_true: files('armsse.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX7', if_true: files('fsl-imx7.c', 'mcimx7d-sabre.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX8MP', if_true: files('fsl-imx8mp.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX8MP_EVK', if_true: files('imx8mp-evk.c'))
arm_common_ss.add(when: 'CONFIG_ARM_SMMUV3', if_true: files('smmuv3.c'))
arm_common_ss.add(when: 'CONFIG_FSL_IMX6UL', if_true: files('fsl-imx6ul.c', 'mcimx6ul-evk.c'))
arm_common_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_soc.c'))
arm_common_ss.add(when: 'CONFIG_XEN', if_true: files(
  'xen-stubs.c',
  'xen-pvh.c',
))

arm_common_ss.add(when: 'CONFIG_ARM_SMMUV3', if_true: files('smmu-common.c'))
arm_common_ss.add(when: 'CONFIG_COLLIE', if_true: files('collie.c'))
arm_common_ss.add(when: 'CONFIG_EXYNOS4', if_true: files('exynos4_boards.c'))
arm_common_ss.add(when: 'CONFIG_MAX78000FTHR', if_true: files('max78000fthr.c'))
arm_common_ss.add(when: 'CONFIG_NETDUINO2', if_true: files('netduino2.c'))
arm_common_ss.add(when: 'CONFIG_RASPI', if_true: files('bcm2835_peripherals.c'))
arm_common_ss.add(when: 'CONFIG_RASPI', if_true: files('bcm2838_peripherals.c'))
arm_common_ss.add(when: 'CONFIG_STRONGARM', if_true: files('strongarm.c'))
arm_common_ss.add(when: 'CONFIG_SX1', if_true: files('omap_sx1.c'))
arm_common_ss.add(when: 'CONFIG_VERSATILE', if_true: files('versatilepb.c'))
arm_common_ss.add(when: 'CONFIG_VEXPRESS', if_true: files('vexpress.c'))

arm_common_ss.add(files('boot.c'))

hw_arch += {'arm': arm_ss}
hw_common_arch += {'arm': arm_common_ss}
