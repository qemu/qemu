hexagon_ss = ss.source_set()
hexagon_ss.add(files('hexagon_tlb.c'))
hexagon_ss.add(files('hexagon_globalreg.c'))
hexagon_ss.add(files('hexagon_hvx_context.c'))
hexagon_ss.add(when: 'CONFIG_HEX_DSP', if_true: files('hex-subsys.c'))
hexagon_ss.add(when: 'CONFIG_HEX_DSP', if_true: files('hexagon_dsp.c'))
hexagon_ss.add(when: 'CONFIG_HEX_BOARD', if_true: files('hex-board.c'))
hexagon_ss.add(when: 'CONFIG_HEX_VIRT', if_true: files('virt.c'))
hexagon_ss.add(when: 'CONFIG_HEX_QCOM_DSP', if_true: files('qcom_dsp.c'))
hexagon_ss.add(when: 'CONFIG_HEX_QCS6490_CDSP', if_true: files('qcs6490_cdsp.c'))
hexagon_ss.add(when: 'CONFIG_HEX_SA8775P_CDSP', if_true: files('sa8775p_cdsp.c'))
hexagon_ss.add(when: 'CONFIG_HEX_SC8480XP_NSP0', if_true: files('sc8480xp_nsp0.c'))

hw_arch += {'hexagon': hexagon_ss}
