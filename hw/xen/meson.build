system_ss.add(when: ['CONFIG_XEN_BUS'], if_true: files(
  'xen-backend.c',
  'xen-bus-helper.c',
  'xen-bus.c',
  'xen-legacy-backend.c',
  'xen_devconfig.c',
  'xen_pvdev.c',
))

xen_common_ss = ss.source_set()
xen_common_ss.add(files(
  'xen-hvm-common.c',
  'xen-mapcache.c',
  'xen-operations.c',
  'xen-pvh-common.c',
))

if have_xen_pci_passthrough
  xen_common_ss.add(files(
    'xen-host-pci-device.c',
    'xen_pt.c',
    'xen_pt_config_init.c',
    'xen_pt_graphics.c',
    'xen_pt_load_rom.c',
    'xen_pt_msi.c',
  ))
else
  xen_common_ss.add(files('xen_pt_stub.c'))
endif

system_ss.add_all(when: ['CONFIG_XEN', xen], if_true: xen_common_ss)
system_ss.add(when: ['CONFIG_XEN', xen], if_false: files('xen_stubs.c'))
