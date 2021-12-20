# All QTests for now are POSIX-only, but the dependencies are
# really in libqtest, not in the testcases themselves.
if not config_host.has_key('CONFIG_POSIX')
  subdir_done()
endif

slow_qtests = {
  'ahci-test' : 60,
  'bios-tables-test' : 120,
  'boot-serial-test' : 60,
  'migration-test' : 150,
  'npcm7xx_pwm-test': 150,
  'prom-env-test' : 60,
  'pxe-test' : 60,
  'qos-test' : 60,
  'qom-test' : 300,
  'test-hmp' : 120,
}

qtests_generic = \
  (config_all_devices.has_key('CONFIG_MEGASAS_SCSI_PCI') ? ['fuzz-megasas-test'] : []) + \
  (config_all_devices.has_key('CONFIG_LSI_SCSI_PCI') ? ['fuzz-lsi53c895a-test'] : []) + \
  (config_all_devices.has_key('CONFIG_VIRTIO_SCSI') ? ['fuzz-virtio-scsi-test'] : []) + \
  (config_all_devices.has_key('CONFIG_SB16') ? ['fuzz-sb16-test'] : []) + \
  (config_all_devices.has_key('CONFIG_SDHCI_PCI') ? ['fuzz-sdcard-test'] : []) + \
  [
  'cdrom-test',
  'device-introspect-test',
  'machine-none-test',
  'qmp-test',
  'qmp-cmd-test',
  'qom-test',
  'test-hmp',
  'qos-test',
]
if config_host.has_key('CONFIG_MODULES')
  qtests_generic += [ 'modules-test' ]
endif

qtests_pci = \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_IVSHMEM_DEVICE') ? ['ivshmem-test'] : [])

qtests_i386 = \
  (slirp.found() ? ['pxe-test', 'test-netfilter'] : []) +             \
  (config_host.has_key('CONFIG_POSIX') ? ['test-filter-mirror'] : []) +                     \
  (have_tools ? ['ahci-test'] : []) +                                                       \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +           \
  (config_all_devices.has_key('CONFIG_SGA') ? ['boot-serial-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_ISA_IPMI_KCS') ? ['ipmi-kcs-test'] : []) +            \
  (config_host.has_key('CONFIG_LINUX') and                                                  \
   config_all_devices.has_key('CONFIG_ISA_IPMI_BT') ? ['ipmi-bt-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_WDT_IB700') ? ['wdt_ib700-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_PVPANIC_ISA') ? ['pvpanic-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_PVPANIC_PCI') ? ['pvpanic-pci-test'] : []) +          \
  (config_all_devices.has_key('CONFIG_HDA') ? ['intel-hda-test'] : []) +                    \
  (config_all_devices.has_key('CONFIG_I82801B11') ? ['i82801b11-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_IOH3420') ? ['ioh3420-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_LPC_ICH9') ? ['lpc-ich9-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_USB_UHCI') ? ['usb-hcd-uhci-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_USB_UHCI') and                                        \
   config_all_devices.has_key('CONFIG_USB_EHCI') ? ['usb-hcd-ehci-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_USB_XHCI_NEC') ? ['usb-hcd-xhci-test'] : []) +        \
  (config_all_devices.has_key('CONFIG_TPM_CRB') ? ['tpm-crb-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_TPM_CRB') ? ['tpm-crb-swtpm-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_TPM_TIS_ISA') ? ['tpm-tis-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_TPM_TIS_ISA') ? ['tpm-tis-swtpm-test'] : []) +        \
  (config_all_devices.has_key('CONFIG_RTL8139_PCI') ? ['rtl8139-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_E1000E_PCI_EXPRESS') ? ['fuzz-e1000e-test'] : []) +   \
  (config_all_devices.has_key('CONFIG_ESP_PCI') ? ['am53c974-test'] : []) +                 \
  (config_all_devices.has_key('CONFIG_VIRTIO_NET') and                                      \
   config_all_devices.has_key('CONFIG_Q35') and                                             \
   config_all_devices.has_key('CONFIG_VIRTIO_PCI') and                                      \
   slirp.found() ? ['virtio-net-failover'] : []) +                                          \
  (unpack_edk2_blobs ? ['bios-tables-test'] : []) +                                         \
  qtests_pci +                                                                              \
  ['fdc-test',
   'ide-test',
   'hd-geo-test',
   'boot-order-test',
   'rtc-test',
   'i440fx-test',
   'fw_cfg-test',
   'device-plug-test',
   'drive_del-test',
   'tco-test',
   'cpu-plug-test',
   'q35-test',
   'vmgenid-test',
   'migration-test',
   'test-x86-cpuid-compat',
   'numa-test',
   'test-filter-redirector'
  ]

if dbus_display
  qtests_i386 += ['dbus-display-test']
endif

dbus_daemon = find_program('dbus-daemon', required: false)
if dbus_daemon.found() and config_host.has_key('GDBUS_CODEGEN')
  # Temporarily disabled due to Patchew failures:
  #qtests_i386 += ['dbus-vmstate-test']
  dbus_vmstate1 = custom_target('dbus-vmstate description',
                                output: ['dbus-vmstate1.h', 'dbus-vmstate1.c'],
                                input: meson.source_root() / 'backends/dbus-vmstate1.xml',
                                command: [config_host['GDBUS_CODEGEN'],
                                          '@INPUT@',
                                          '--interface-prefix', 'org.qemu',
                                          '--generate-c-code', '@BASENAME@']).to_list()
else
  dbus_vmstate1 = []
endif

qtests_x86_64 = qtests_i386

qtests_alpha = ['boot-serial-test'] + \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : []) + \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_avr = [ 'boot-serial-test' ]

qtests_hppa = ['boot-serial-test'] + \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : []) + \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_m68k = ['boot-serial-test'] + \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : [])

qtests_microblaze = ['boot-serial-test'] + \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : [])

qtests_microblazeel = qtests_microblaze

qtests_mips = \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : []) + \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_mips64 = \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : []) + \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_mips64el = \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : []) + \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_ppc = \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : []) + \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_M48T59') ? ['m48t59-test'] : []) +                     \
  ['boot-order-test', 'prom-env-test', 'boot-serial-test']                 \

qtests_ppc64 = \
  qtests_ppc + \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['device-plug-test'] : []) +               \
  (config_all_devices.has_key('CONFIG_POWERNV') ? ['pnv-xscom-test'] : []) +                 \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['rtas-test'] : []) +                      \
  (slirp.found() ? ['pxe-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_USB_UHCI') ? ['usb-hcd-uhci-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_USB_XHCI_NEC') ? ['usb-hcd-xhci-test'] : []) +         \
  qtests_pci + ['migration-test', 'numa-test', 'cpu-plug-test', 'drive_del-test']

qtests_sh4 = (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : [])
qtests_sh4eb = (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : [])

qtests_sparc = ['prom-env-test', 'm48t59-test', 'boot-serial-test'] + \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  (slirp.found() ? ['test-netfilter'] : [])

qtests_sparc64 = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (slirp.found() ? ['test-netfilter'] : []) + \
  ['test-filter-mirror', 'test-filter-redirector'] + \
  ['prom-env-test', 'boot-serial-test']

qtests_npcm7xx = \
  ['npcm7xx_adc-test',
   'npcm7xx_gpio-test',
   'npcm7xx_pwm-test',
   'npcm7xx_rng-test',
   'npcm7xx_smbus-test',
   'npcm7xx_timer-test',
   'npcm7xx_watchdog_timer-test'] + \
   (slirp.found() ? ['npcm7xx_emc-test'] : [])
qtests_aspeed = \
  ['aspeed_hace-test',
   'aspeed_smc-test']
qtests_arm = \
  (config_all_devices.has_key('CONFIG_MPS2') ? ['sse-timer-test'] : []) + \
  (config_all_devices.has_key('CONFIG_CMSDK_APB_DUALTIMER') ? ['cmsdk-apb-dualtimer-test'] : []) + \
  (config_all_devices.has_key('CONFIG_CMSDK_APB_TIMER') ? ['cmsdk-apb-timer-test'] : []) + \
  (config_all_devices.has_key('CONFIG_CMSDK_APB_WATCHDOG') ? ['cmsdk-apb-watchdog-test'] : []) + \
  (config_all_devices.has_key('CONFIG_PFLASH_CFI02') ? ['pflash-cfi02-test'] : []) +         \
  (config_all_devices.has_key('CONFIG_ASPEED_SOC') ? qtests_aspeed : []) + \
  (config_all_devices.has_key('CONFIG_NPCM7XX') ? qtests_npcm7xx : []) + \
  ['arm-cpu-features',
   'microbit-test',
   'test-arm-mptimer',
   'boot-serial-test',
   'hexloader-test']

# TODO: once aarch64 TCG is fixed on ARM 32 bit host, make bios-tables-test unconditional
qtests_aarch64 = \
  (cpu != 'arm' and unpack_edk2_blobs ? ['bios-tables-test'] : []) +                            \
  (config_all_devices.has_key('CONFIG_TPM_TIS_SYSBUS') ? ['tpm-tis-device-test'] : []) +        \
  (config_all_devices.has_key('CONFIG_TPM_TIS_SYSBUS') ? ['tpm-tis-device-swtpm-test'] : []) +  \
  (config_all_devices.has_key('CONFIG_XLNX_ZYNQMP_ARM') ? ['xlnx-can-test', 'fuzz-xlnx-dp-test'] : []) + \
  ['arm-cpu-features',
   'numa-test',
   'boot-serial-test',
   'migration-test']

qtests_s390x = \
  (slirp.found() ? ['pxe-test', 'test-netfilter'] : []) +                 \
  (config_host.has_key('CONFIG_POSIX') ? ['test-filter-mirror'] : []) +                         \
  (config_host.has_key('CONFIG_POSIX') ? ['test-filter-redirector'] : []) +                     \
  ['boot-serial-test',
   'drive_del-test',
   'device-plug-test',
   'virtio-ccw-test',
   'cpu-plug-test',
   'migration-test']

qos_test_ss = ss.source_set()
qos_test_ss.add(
  'ac97-test.c',
  'adm1272-test.c',
  'ds1338-test.c',
  'e1000-test.c',
  'e1000e-test.c',
  'eepro100-test.c',
  'es1370-test.c',
  'ipoctal232-test.c',
  'max34451-test.c',
  'megasas-test.c',
  'ne2000-test.c',
  'tulip-test.c',
  'nvme-test.c',
  'pca9552-test.c',
  'pci-test.c',
  'pcnet-test.c',
  'sdhci-test.c',
  'spapr-phb-test.c',
  'tmp105-test.c',
  'emc141x-test.c',
  'usb-hcd-ohci-test.c',
  'virtio-test.c',
  'virtio-blk-test.c',
  'virtio-net-test.c',
  'virtio-rng-test.c',
  'virtio-scsi-test.c',
  'virtio-serial-test.c',
  'virtio-iommu-test.c',
  'vmxnet3-test.c',
)
if have_virtfs
  qos_test_ss.add(files('virtio-9p-test.c'))
endif
qos_test_ss.add(when: 'CONFIG_VHOST_USER', if_true: files('vhost-user-test.c'))
if have_tools and have_vhost_user_blk_server
  qos_test_ss.add(files('vhost-user-blk-test.c'))
endif

tpmemu_files = ['tpm-emu.c', 'tpm-util.c', 'tpm-tests.c']

qtests = {
  'bios-tables-test': [io, 'boot-sector.c', 'acpi-utils.c', 'tpm-emu.c'],
  'cdrom-test': files('boot-sector.c'),
  'dbus-vmstate-test': files('migration-helpers.c') + dbus_vmstate1,
  'ivshmem-test': [rt, '../../contrib/ivshmem-server/ivshmem-server.c'],
  'migration-test': files('migration-helpers.c'),
  'pxe-test': files('boot-sector.c'),
  'qos-test': [chardev, io, qos_test_ss.apply(config_host, strict: false).sources()],
  'tpm-crb-swtpm-test': [io, tpmemu_files],
  'tpm-crb-test': [io, tpmemu_files],
  'tpm-tis-swtpm-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'tpm-tis-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'tpm-tis-device-swtpm-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'tpm-tis-device-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'vmgenid-test': files('boot-sector.c', 'acpi-utils.c'),
}

if dbus_display
qtests += {'dbus-display-test': [dbus_display1, gio]}
endif

qtest_executables = {}
foreach dir : target_dirs
  if not dir.endswith('-softmmu')
    continue
  endif

  target_base = dir.split('-')[0]
  qtest_emulator = emulators['qemu-system-' + target_base]
  target_qtests = get_variable('qtests_' + target_base, []) + qtests_generic

  test_deps = roms
  qtest_env = environment()
  if have_tools
    qtest_env.set('QTEST_QEMU_IMG', './qemu-img')
    test_deps += [qemu_img]
  endif
  qtest_env.set('G_TEST_DBUS_DAEMON', meson.project_source_root() / 'tests/dbus-vmstate-daemon.sh')
  qtest_env.set('QTEST_QEMU_BINARY', './qemu-system-' + target_base)
  if have_tools and have_vhost_user_blk_server
    qtest_env.set('QTEST_QEMU_STORAGE_DAEMON_BINARY', './storage-daemon/qemu-storage-daemon')
    test_deps += [qsd]
  endif

  foreach test : target_qtests
    # Executables are shared across targets, declare them only the first time we
    # encounter them
    if not qtest_executables.has_key(test)
      src = [test + '.c']
      deps = [qemuutil, qos]
      if test in qtests
        # use a sourceset to quickly separate sources and deps
        test_ss = ss.source_set()
        test_ss.add(qtests[test])
        src += test_ss.all_sources()
        deps += test_ss.all_dependencies()
      endif
      qtest_executables += {
        test: executable(test, src, dependencies: deps)
      }
    endif
    # FIXME: missing dependency on the emulator binary and qemu-img
    test('qtest-@0@/@1@'.format(target_base, test),
         qtest_executables[test],
         depends: [test_deps, qtest_emulator],
         env: qtest_env,
         args: ['--tap', '-k'],
         protocol: 'tap',
         timeout: slow_qtests.get(test, 30),
         priority: slow_qtests.get(test, 30),
         suite: ['qtest', 'qtest-' + target_base])
  endforeach
endforeach
