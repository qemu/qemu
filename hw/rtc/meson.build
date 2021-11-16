
softmmu_ss.add(when: 'CONFIG_DS1338', if_true: files('ds1338.c'))
softmmu_ss.add(when: 'CONFIG_M41T80', if_true: files('m41t80.c'))
softmmu_ss.add(when: 'CONFIG_M48T59', if_true: files('m48t59.c'))
specific_ss.add(when: 'CONFIG_PL031', if_true: files('pl031.c'))
softmmu_ss.add(when: 'CONFIG_TWL92230', if_true: files('twl92230.c'))
softmmu_ss.add(when: ['CONFIG_ISA_BUS', 'CONFIG_M48T59'], if_true: files('m48t59-isa.c'))
softmmu_ss.add(when: 'CONFIG_XLNX_ZYNQMP', if_true: files('xlnx-zynqmp-rtc.c'))

softmmu_ss.add(when: 'CONFIG_EXYNOS4', if_true: files('exynos4210_rtc.c'))
softmmu_ss.add(when: 'CONFIG_SUN4V_RTC', if_true: files('sun4v-rtc.c'))
softmmu_ss.add(when: 'CONFIG_ASPEED_SOC', if_true: files('aspeed_rtc.c'))
softmmu_ss.add(when: 'CONFIG_GOLDFISH_RTC', if_true: files('goldfish_rtc.c'))
softmmu_ss.add(when: 'CONFIG_ALLWINNER_H3', if_true: files('allwinner-rtc.c'))

specific_ss.add(when: 'CONFIG_MC146818RTC', if_true: files('mc146818rtc.c'))
