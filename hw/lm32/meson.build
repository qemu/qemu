lm32_ss = ss.source_set()
# LM32 boards
lm32_ss.add(when: 'CONFIG_LM32', if_true: files('lm32_boards.c'))
lm32_ss.add(when: 'CONFIG_MILKYMIST', if_true: files('milkymist.c'))

hw_arch += {'lm32': lm32_ss}
