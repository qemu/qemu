i386_ss.add(when: 'CONFIG_XEN', if_true: files(
  'xen-hvm.c',
  'xen-mapcache.c',
  'xen_apic.c',
  'xen_pvdevice.c',
))

i386_ss.add(when: 'CONFIG_XEN_BUS', if_true: files(
  'xen_platform.c',
))
