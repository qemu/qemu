softmmu_ss.add(when: ['CONFIG_XEN', xen], if_true: files(
  'xen-backend.c',
  'xen-bus-helper.c',
  'xen-bus.c',
  'xen-legacy-backend.c',
  'xen_devconfig.c',
  'xen_pvdev.c',
))

xen_specific_ss = ss.source_set()
if have_xen_pci_passthrough
  xen_specific_ss.add(files(
    'xen-host-pci-device.c',
    'xen_pt.c',
    'xen_pt_config_init.c',
    'xen_pt_graphics.c',
    'xen_pt_load_rom.c',
    'xen_pt_msi.c',
  ))
else
  xen_specific_ss.add(files('xen_pt_stub.c'))
endif

specific_ss.add_all(when: ['CONFIG_XEN', xen], if_true: xen_specific_ss)
