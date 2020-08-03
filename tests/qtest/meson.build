# All QTests for now are POSIX-only, but the dependencies are
# really in libqtest, not in the testcases themselves.
if not config_host.has_key('CONFIG_POSIX')
  subdir_done()
endif

qtests_generic = [
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
  (config_host.has_key('CONFIG_SLIRP') ? ['pxe-test', 'test-netfilter'] : []) +             \
  (config_host.has_key('CONFIG_POSIX') ? ['test-filter-mirror'] : []) +                     \
  (have_tools ? ['ahci-test'] : []) +                                                       \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +           \
  (config_all_devices.has_key('CONFIG_SGA') ? ['boot-serial-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_RTL8139_PCI') ? ['test-filter-redirector'] : []) +    \
  (config_all_devices.has_key('CONFIG_ISA_IPMI_KCS') ? ['ipmi-kcs-test'] : []) +            \
  (config_host.has_key('CONFIG_LINUX') and                                                  \
   config_all_devices.has_key('CONFIG_ISA_IPMI_BT') ? ['ipmi-bt-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_WDT_IB700') ? ['wdt_ib700-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_PVPANIC') ? ['pvpanic-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_HDA') ? ['intel-hda-test'] : []) +                    \
  (config_all_devices.has_key('CONFIG_I82801B11') ? ['i82801b11-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_IOH3420') ? ['ioh3420-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_USB_UHCI') ? ['usb-hcd-uhci-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_USB_UHCI') and                                        \
   config_all_devices.has_key('CONFIG_USB_EHCI') ? ['usb-hcd-ehci-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_USB_XHCI_NEC') ? ['usb-hcd-xhci-test'] : []) +        \
  (config_all_devices.has_key('CONFIG_TPM_CRB') ? ['tpm-crb-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_TPM_CRB') ? ['tpm-crb-swtpm-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_TPM_TIS_ISA') ? ['tpm-tis-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_TPM_TIS_ISA') ? ['tpm-tis-swtpm-test'] : []) +        \
  (config_all_devices.has_key('CONFIG_RTL8139_PCI') ? ['rtl8139-test'] : []) +              \
  qtests_pci +                                                                              \
  ['fdc-test',
   'ide-test',
   'ahci-test',
   'hd-geo-test',
   'boot-order-test',
   'bios-tables-test',
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
   'numa-test']

dbus_daemon = find_program('dbus-daemon', required: false)
if dbus_daemon.found() and config_host.has_key('GDBUS_CODEGEN')
  # Temporarily disabled due to Patchew failures:
  #qtests_i386 += ['dbus-vmstate-test']
  dbus_vmstate1 = custom_target('dbus-vmstate description',
                                output: ['dbus-vmstate1.h', 'dbus-vmstate1.c'],
                                input: files('dbus-vmstate1.xml'),
                                command: [config_host['GDBUS_CODEGEN'],
                                          '@INPUT@',
                                          '--interface-prefix', 'org.qemu',
                                          '--generate-c-code', '@BASENAME@']).to_list()
else
  dbus_vmstate1 = []
endif

qtests_x86_64 = qtests_i386

qtests_alpha = [ 'boot-serial-test' ] +                                                      \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_avr = [ 'boot-serial-test' ]

qtests_hppa = [ 'boot-serial-test' ] +                                                       \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_m68k = [ 'boot-serial-test' ]
qtests_microblaze = [ 'boot-serial-test' ]
qtests_microblazeel = qtests_microblaze

qtests_mips = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_mips64 = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_mips64el = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_moxie = [ 'boot-serial-test' ]

qtests_ppc = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_M48T59') ? ['m48t59-test'] : []) +                     \
  ['boot-order-test', 'prom-env-test', 'drive_del-test', 'boot-serial-test']                 \

qtests_ppc64 = \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['device-plug-test'] : []) +               \
  (config_all_devices.has_key('CONFIG_POWERNV') ? ['pnv-xscom-test'] : []) +                 \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['rtas-test'] : []) +                      \
  (config_host.has_key('CONFIG_SLIRP') ? ['pxe-test', 'test-netfilter'] : []) +              \
  (config_all_devices.has_key('CONFIG_USB_UHCI') ? ['usb-hcd-uhci-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_USB_XHCI_NEC') ? ['usb-hcd-xhci-test'] : []) +         \
  (config_host.has_key('CONFIG_POSIX') ? ['test-filter-mirror'] : []) +                      \
  qtests_pci + ['migration-test', 'numa-test', 'cpu-plug-test']

qtests_sh4 = (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : [])
qtests_sh4eb = (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : [])

qtests_sparc = ['prom-env-test', 'm48t59-test', 'boot-serial-test']

qtests_sparc64 = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  ['prom-env-test', 'boot-serial-test']

qtests_arm = \
  (config_all_devices.has_key('CONFIG_PFLASH_CFI02') ? ['pflash-cfi02-test'] : []) +         \
  ['arm-cpu-features',
   'microbit-test',
   'm25p80-test',
   'test-arm-mptimer',
   'boot-serial-test',
   'hexloader-test']

# TODO: once aarch64 TCG is fixed on ARM 32 bit host, make bios-tables-test unconditional
qtests_aarch64 = \
  (cpu != 'arm' ? ['bios-tables-test'] : []) +                                                  \
  (config_all_devices.has_key('CONFIG_TPM_TIS_SYSBUS') ? ['tpm-tis-device-test'] : []) +        \
  (config_all_devices.has_key('CONFIG_TPM_TIS_SYSBUS') ? ['tpm-tis-device-swtpm-test'] : []) +  \
  ['numa-test',
   'boot-serial-test',
   'migration-test']

qtests_s390x = \
  (config_host.has_key('CONFIG_SLIRP') ? ['pxe-test', 'test-netfilter'] : []) +                 \
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
  'ds1338-test.c',
  'e1000-test.c',
  'e1000e-test.c',
  'eepro100-test.c',
  'es1370-test.c',
  'ipoctal232-test.c',
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
  'usb-hcd-ohci-test.c',
  'virtio-test.c',
  'virtio-blk-test.c',
  'virtio-net-test.c',
  'virtio-rng-test.c',
  'virtio-scsi-test.c',
  'virtio-serial-test.c',
  'vmxnet3-test.c',
)
qos_test_ss.add(when: 'CONFIG_VIRTFS', if_true: files('virtio-9p-test.c'))
qos_test_ss.add(when: 'CONFIG_VHOST_USER', if_true: files('vhost-user-test.c'))

extra_qtest_deps = {
  'bios-tables-test': [io],
  'ivshmem-test': [rt],
  'qos-test': [chardev, io],
  'tpm-crb-swtpm-test': [io],
  'tpm-crb-test': [io],
  'tpm-tis-swtpm-test': [io],
  'tpm-tis-test': [io],
  'tpm-tis-device-swtpm-test': [io],
  'tpm-tis-device-test': [io],
}
extra_qtest_srcs = {
  'bios-tables-test': files('boot-sector.c', 'acpi-utils.c', 'tpm-emu.c'),
  'pxe-test': files('boot-sector.c'),
  'cdrom-test': files('boot-sector.c'),
  'migration-test': files('migration-helpers.c'),
  'ivshmem-test': files('../../contrib/ivshmem-server/ivshmem-server.c'),
  'dbus-vmstate-test': files('migration-helpers.c') + dbus_vmstate1,
  'vmgenid-test': files('boot-sector.c', 'acpi-utils.c'),
  'tpm-crb-swtpm-test': files('tpm-emu.c', 'tpm-util.c', 'tpm-tests.c'),
  'tpm-crb-test': files('tpm-emu.c', 'tpm-util.c', 'tpm-tests.c'),
  'tpm-tis-device-swtpm-test': files('tpm-emu.c', 'tpm-util.c', 'tpm-tis-util.c', 'tpm-tests.c'),
  'tpm-tis-device-test': files('tpm-emu.c', 'tpm-util.c', 'tpm-tis-util.c', 'tpm-tests.c'),
  'tpm-tis-swtpm-test': files('tpm-emu.c', 'tpm-util.c', 'tpm-tis-util.c', 'tpm-tests.c'),
  'tpm-tis-test': files('tpm-emu.c', 'tpm-util.c', 'tpm-tis-util.c', 'tpm-tests.c'),
  'qos-test': qos_test_ss.apply(config_host, strict: false).sources()
}


qtest_executables = {}
foreach dir : target_dirs
  if not dir.endswith('-softmmu')
    continue
  endif

  target_base = dir.split('-')[0]
  qtests = get_variable('qtests_' + target_base, []) + qtests_generic

  qtest_env = environment()
  qtest_env.set('QTEST_QEMU_IMG', './qemu-img')
  qtest_env.set('G_TEST_DBUS_DAEMON', meson.source_root() / 'tests/dbus-vmstate-daemon.sh')
  qtest_env.set('QTEST_QEMU_BINARY', './qemu-system-' + target_base)
  
  foreach test : qtests
    # Executables are shared across targets, declare them only the first time we
    # encounter them
    if not qtest_executables.has_key(test)
      qtest_executables += {
        test: executable(test,
                         files(test + '.c') + extra_qtest_srcs.get(test, []),
                         dependencies: [qemuutil, qos] + extra_qtest_deps.get(test, []))
      }
    endif
    # FIXME: missing dependency on the emulator binary and qemu-img
    test('qtest-@0@: @1@'.format(target_base, test),
         qtest_executables[test],
         env: qtest_env,
         args: ['--tap', '-k'],
         protocol: 'tap',
         suite: ['qtest', 'qtest-' + target_base])
  endforeach
endforeach
