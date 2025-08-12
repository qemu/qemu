system_ss.add(files('fw_cfg-interface.c'))
system_ss.add(files('fw_cfg.c'))
system_ss.add(when: 'CONFIG_RASPI', if_true: files('bcm2835_otp.c'))
system_ss.add(when: 'CONFIG_CHRP_NVRAM', if_true: files('chrp_nvram.c'))
system_ss.add(when: 'CONFIG_DS1225Y', if_true: files('ds1225y.c'))
system_ss.add(when: 'CONFIG_NMC93XX_EEPROM', if_true: files('eeprom93xx.c'))
system_ss.add(when: 'CONFIG_AT24C', if_true: files('eeprom_at24c.c'))
system_ss.add(when: 'CONFIG_MAC_NVRAM', if_true: files('mac_nvram.c'))
system_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx_otp.c'))
system_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_nvm.c'))
system_ss.add(when: 'CONFIG_XLNX_EFUSE_CRC', if_true: files('xlnx-efuse-crc.c'))
system_ss.add(when: 'CONFIG_XLNX_EFUSE', if_true: files('xlnx-efuse.c'))
system_ss.add(when: 'CONFIG_XLNX_EFUSE_VERSAL', if_true: files(
                                                   'xlnx-versal-efuse-cache.c',
                                                   'xlnx-versal-efuse-ctrl.c'))
system_ss.add(when: 'CONFIG_XLNX_EFUSE_ZYNQMP', if_true: files(
                                                   'xlnx-zynqmp-efuse.c'))
system_ss.add(when: 'CONFIG_XLNX_BBRAM', if_true: files('xlnx-bbram.c'))

specific_ss.add(when: 'CONFIG_PSERIES', if_true: files('spapr_nvram.c'))
specific_ss.add(when: 'CONFIG_ACPI', if_true: files('fw_cfg-acpi.c'))

system_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files(
  'aspeed_otp.c',
  ))