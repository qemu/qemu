system_ss.add(when: 'CONFIG_GPIO_KEY', if_true: files('gpio_key.c'))
system_ss.add(when: 'CONFIG_GPIO_MPC8XXX', if_true: files('mpc8xxx.c'))
system_ss.add(when: 'CONFIG_GPIO_PWR', if_true: files('gpio_pwr.c'))
system_ss.add(when: 'CONFIG_PCA9552', if_true: files('pca9552.c'))
system_ss.add(when: 'CONFIG_PCA9554', if_true: files('pca9554.c'))
system_ss.add(when: 'CONFIG_PL061', if_true: files('pl061.c'))
system_ss.add(when: 'CONFIG_ZAURUS_SCOOP', if_true: files('zaurus.c'))

system_ss.add(when: 'CONFIG_IMX', if_true: files('imx_gpio.c'))
system_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx_gpio.c'))
system_ss.add(when: 'CONFIG_NRF51_SOC', if_true: files('nrf51_gpio.c'))
system_ss.add(when: 'CONFIG_OMAP', if_true: files('omap_gpio.c'))
system_ss.add(when: 'CONFIG_RASPI', if_true: files(
    'bcm2835_gpio.c',
    'bcm2838_gpio.c'
))
system_ss.add(when: 'CONFIG_STM32L4X5_SOC', if_true: files('stm32l4x5_gpio.c'))
system_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files('aspeed_gpio.c'))
system_ss.add(when: 'CONFIG_SIFIVE_GPIO', if_true: files('sifive_gpio.c'))
system_ss.add(when: 'CONFIG_PCF8574', if_true: files('pcf8574.c'))
