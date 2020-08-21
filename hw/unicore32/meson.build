unicore32_ss = ss.source_set()
# PKUnity-v3 SoC and board information
unicore32_ss.add(when: 'CONFIG_PUV3', if_true: files('puv3.c'))

hw_arch += {'unicore32': unicore32_ss}
