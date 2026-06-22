hexagon_ss = ss.source_set()
hexagon_ss.add(files('hexagon_tlb.c'))
hexagon_ss.add(files('hexagon_globalreg.c'))
hexagon_ss.add(when: 'CONFIG_HEX_DSP', if_true: files('hexagon_dsp.c'))

hw_arch += {'hexagon': hexagon_ss}
