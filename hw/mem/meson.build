mem_ss = ss.source_set()
mem_ss.add(files('memory-device.c'))
mem_ss.add(when: 'CONFIG_DIMM', if_true: files('pc-dimm.c'))
mem_ss.add(when: 'CONFIG_NPCM7XX', if_true: files('npcm7xx_mc.c'))
mem_ss.add(when: 'CONFIG_NVDIMM', if_true: files('nvdimm.c'))
mem_ss.add(when: 'CONFIG_CXL_MEM_DEVICE', if_true: files('cxl_type3.c'))
system_ss.add(when: 'CONFIG_CXL_MEM_DEVICE', if_false: files('cxl_type3_stubs.c'))

system_ss.add_all(when: 'CONFIG_MEM_DEVICE', if_true: mem_ss)

system_ss.add(when: 'CONFIG_SPARSE_MEM', if_true: files('sparse-mem.c'))
