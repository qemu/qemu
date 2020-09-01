alpha_ss = ss.source_set()
alpha_ss.add(when: 'CONFIG_DP264', if_true: files(
  'dp264.c',
  'pci.c',
  'typhoon.c',
))

hw_arch += {'alpha': alpha_ss}
