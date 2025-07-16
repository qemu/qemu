slow_qtests = {
  'ahci-test': 150,
  'aspeed_smc-test': 360,
  'bios-tables-test' : 910,
  'cdrom-test' : 610,
  'device-introspect-test' : 720,
  'ide-test' : 120,
  'migration-test' : 480,
  'npcm7xx_pwm-test': 300,
  'npcm7xx_watchdog_timer-test': 120,
  'qmp-cmd-test' : 120,
  'qom-test' : 900,
  'stm32l4x5_usart-test' : 600,
  'test-hmp' : 240,
  'pxe-test': 610,
  'prom-env-test': 360,
  'boot-serial-test': 360,
  'qos-test': 120,
  'vmgenid-test': 610,
}

qtests_generic = [
  'cdrom-test',
  'device-introspect-test',
  'machine-none-test',
  'qmp-test',
  'qmp-cmd-test',
  'qom-test',
  'test-hmp',
  'qos-test',
  'readconfig-test',
  'netdev-socket',
]
if enable_modules
  qtests_generic += [ 'modules-test' ]
endif

qtests_pci = \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_IVSHMEM_DEVICE') ? ['ivshmem-test'] : [])

qtests_cxl = \
  (config_all_devices.has_key('CONFIG_CXL') ? ['cxl-test'] : [])

# FIXME: Get rid of get_option('default_devices') here and check
#        for the availability of the default NICs in the tests
qtests_filter = \
  (get_option('default_devices') and slirp.found() ? ['test-netfilter'] : []) + \
  (get_option('default_devices') and host_os != 'windows' ? ['test-filter-mirror'] : []) + \
  (get_option('default_devices') and host_os != 'windows' ? ['test-filter-redirector'] : [])

qtests_i386 = \
  (slirp.found() ? ['pxe-test'] : []) + \
  qtests_filter + \
  (config_all_devices.has_key('CONFIG_ACPI_VMGENID') ? ['vmgenid-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_AHCI_ICH9') and have_tools ? ['ahci-test'] : []) +    \
  (config_all_devices.has_key('CONFIG_AHCI_ICH9') ? ['tco-test'] : []) +                    \
  (config_all_devices.has_key('CONFIG_FDC_ISA') ? ['fdc-test'] : []) +                      \
  (config_all_devices.has_key('CONFIG_I440FX') ? ['fw_cfg-test'] : []) +                    \
  (config_all_devices.has_key('CONFIG_FW_CFG_DMA') ? ['vmcoreinfo-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_I440FX') ? ['i440fx-test'] : []) +                    \
  (config_all_devices.has_key('CONFIG_I440FX') ? ['ide-test'] : []) +                       \
  (config_all_devices.has_key('CONFIG_I440FX') ? ['numa-test'] : []) +                      \
  (config_all_devices.has_key('CONFIG_I440FX') ? ['test-x86-cpuid-compat'] : []) +          \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +           \
  (config_all_devices.has_key('CONFIG_SGA') ? ['boot-serial-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_ISA_IPMI_KCS') ? ['ipmi-kcs-test'] : []) +            \
  (host_os == 'linux' and                                                                  \
   config_all_devices.has_key('CONFIG_ISA_IPMI_BT') and
   config_all_devices.has_key('CONFIG_IPMI_EXTERN') ? ['ipmi-bt-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_WDT_IB700') ? ['wdt_ib700-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_PVPANIC_ISA') ? ['pvpanic-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_PVPANIC_PCI') ? ['pvpanic-pci-test'] : []) +          \
  (config_all_devices.has_key('CONFIG_HDA') ? ['intel-hda-test'] : []) +                    \
  (config_all_devices.has_key('CONFIG_I82801B11') ? ['i82801b11-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_IOH3420') ? ['ioh3420-test'] : []) +                  \
  (config_all_devices.has_key('CONFIG_LPC_ICH9') ? ['lpc-ich9-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_MC146818RTC') ? ['rtc-test'] : []) +                  \
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
  (config_all_devices.has_key('CONFIG_MEGASAS_SCSI_PCI') ? ['fuzz-megasas-test'] : []) +    \
  (config_all_devices.has_key('CONFIG_LSI_SCSI_PCI') ? ['fuzz-lsi53c895a-test'] : []) +     \
  (config_all_devices.has_key('CONFIG_VIRTIO_SCSI') ? ['fuzz-virtio-scsi-test'] : []) +     \
  (config_all_devices.has_key('CONFIG_VIRTIO_BALLOON') ? ['virtio-balloon-test'] : []) + \
  (config_all_devices.has_key('CONFIG_Q35') ? ['q35-test'] : []) +                          \
  (config_all_devices.has_key('CONFIG_SB16') ? ['fuzz-sb16-test'] : []) +                   \
  (config_all_devices.has_key('CONFIG_SDHCI_PCI') ? ['fuzz-sdcard-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_ESP_PCI') ? ['am53c974-test'] : []) +                 \
  (config_all_devices.has_key('CONFIG_VTD') ? ['intel-iommu-test'] : []) +                 \
  (host_os != 'windows' and                                                                \
   config_all_devices.has_key('CONFIG_ACPI_ERST') ? ['erst-test'] : []) +                   \
  (config_all_devices.has_key('CONFIG_PCIE_PORT') and                                       \
   config_all_devices.has_key('CONFIG_VIRTIO_NET') and                                      \
   config_all_devices.has_key('CONFIG_Q35') and                                             \
   config_all_devices.has_key('CONFIG_VIRTIO_PCI') and                                      \
   slirp.found() ? ['virtio-net-failover'] : []) +                                          \
  (unpack_edk2_blobs and                                                                    \
   config_all_devices.has_key('CONFIG_HPET') and                                            \
   config_all_devices.has_key('CONFIG_PARALLEL') ? ['bios-tables-test'] : []) +             \
  qtests_pci +                                                                              \
  qtests_cxl +                                                                              \
  [
   'hd-geo-test',
   'boot-order-test',
   'device-plug-test',
   'drive_del-test',
   'cpu-plug-test',
   'migration-test',
  ]

if dbus_display and config_all_devices.has_key('CONFIG_VGA')
  qtests_i386 += ['dbus-display-test']
endif

dbus_daemon = find_program('dbus-daemon', required: false)
if dbus_daemon.found() and gdbus_codegen.found()
  # Temporarily disabled due to Patchew failures:
  #qtests_i386 += ['dbus-vmstate-test']
  dbus_vmstate1 = custom_target('dbus-vmstate description',
                                output: ['dbus-vmstate1.h', 'dbus-vmstate1.c'],
                                input: meson.project_source_root() / 'backends/dbus-vmstate1.xml',
                                command: [gdbus_codegen, '@INPUT@',
                                          '--interface-prefix', 'org.qemu',
                                          '--generate-c-code', '@BASENAME@']).to_list()
else
  dbus_vmstate1 = []
endif

qtests_x86_64 = qtests_i386

qtests_alpha = ['boot-serial-test'] + \
  qtests_filter + \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_avr = [ 'boot-serial-test' ]

qtests_hppa = \
  qtests_filter + \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_loongarch64 = qtests_filter + \
  (config_all_devices.has_key('CONFIG_LOONGARCH_VIRT') ? ['numa-test'] : []) + \
  (unpack_edk2_blobs ? ['bios-tables-test'] : []) + \
  ['boot-serial-test',
   'cpu-plug-test']

qtests_m68k = ['boot-serial-test'] + \
  qtests_filter

qtests_microblaze = ['boot-serial-test'] + \
  qtests_filter

qtests_microblazeel = qtests_microblaze

qtests_mips = \
  qtests_filter + \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_devices.has_key('CONFIG_VGA') ? ['display-vga-test'] : [])

qtests_mipsel = qtests_mips
qtests_mips64 = qtests_mips
qtests_mips64el = qtests_mips

qtests_ppc = \
  qtests_filter + \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  (config_all_accel.has_key('CONFIG_TCG') ? ['prom-env-test'] : []) +                              \
  (config_all_accel.has_key('CONFIG_TCG') ? ['boot-serial-test'] : []) +                           \
  ['boot-order-test']

qtests_ppc64 = \
  qtests_ppc + \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['device-plug-test'] : []) +               \
  (config_all_devices.has_key('CONFIG_POWERNV') ? ['pnv-xscom-test'] : []) +                 \
  (config_all_devices.has_key('CONFIG_POWERNV') ? ['pnv-xive2-test'] : []) +                 \
  (config_all_devices.has_key('CONFIG_POWERNV') ? ['pnv-spi-seeprom-test'] : []) +           \
  (config_all_devices.has_key('CONFIG_POWERNV') ? ['pnv-host-i2c-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['numa-test'] : []) +                      \
  (config_all_devices.has_key('CONFIG_PSERIES') ? ['rtas-test'] : []) +                      \
  (slirp.found() ? ['pxe-test'] : []) +              \
  (config_all_devices.has_key('CONFIG_USB_UHCI') ? ['usb-hcd-uhci-test'] : []) +             \
  (config_all_devices.has_key('CONFIG_USB_XHCI_NEC') ? ['usb-hcd-xhci-test'] : []) +         \
  qtests_pci + ['migration-test', 'cpu-plug-test', 'drive_del-test']

qtests_sh4 = (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : [])
qtests_sh4eb = (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : [])

qtests_sparc = ['prom-env-test', 'm48t59-test', 'boot-serial-test'] + \
  qtests_filter

qtests_sparc64 = \
  (config_all_devices.has_key('CONFIG_ISA_TESTDEV') ? ['endianness-test'] : []) +            \
  qtests_filter + \
  ['prom-env-test', 'boot-serial-test']

qtests_npcm7xx = \
  ['npcm7xx_adc-test',
   'npcm7xx_gpio-test',
   'npcm7xx_pwm-test',
   'npcm7xx_rng-test',
   'npcm7xx_sdhci-test',
   'npcm7xx_smbus-test',
   'npcm7xx_timer-test',
   'npcm7xx_watchdog_timer-test'] + \
   (slirp.found() ? ['npcm7xx_emc-test'] : [])
qtests_npcm8xx = \
  ['npcm_gmac-test']
qtests_aspeed = \
  ['aspeed_gpio-test',
   'aspeed_hace-test',
   'aspeed_scu-test',
   'aspeed_smc-test']
qtests_aspeed64 = \
  ['ast2700-gpio-test',
   'ast2700-hace-test',
   'ast2700-smc-test']

qtests_stm32l4x5 = \
  ['stm32l4x5_exti-test',
   'stm32l4x5_syscfg-test',
   'stm32l4x5_rcc-test',
   'stm32l4x5_gpio-test',
   'stm32l4x5_usart-test']

qtests_arm = \
  (config_all_devices.has_key('CONFIG_MPS2') ? ['sse-timer-test'] : []) + \
  (config_all_devices.has_key('CONFIG_CMSDK_APB_DUALTIMER') ? ['cmsdk-apb-dualtimer-test'] : []) + \
  (config_all_devices.has_key('CONFIG_CMSDK_APB_TIMER') ? ['cmsdk-apb-timer-test'] : []) + \
  (config_all_devices.has_key('CONFIG_STELLARIS') or
   config_all_devices.has_key('CONFIG_MPS2') ? ['cmsdk-apb-watchdog-test'] : []) + \
  (config_all_devices.has_key('CONFIG_PFLASH_CFI02') and
   config_all_devices.has_key('CONFIG_MUSICPAL') ? ['pflash-cfi02-test'] : []) + \
  (config_all_devices.has_key('CONFIG_ASPEED_SOC') ? qtests_aspeed : []) + \
  (config_all_devices.has_key('CONFIG_NPCM7XX') ? qtests_npcm7xx : []) + \
  (config_all_devices.has_key('CONFIG_GENERIC_LOADER') ? ['hexloader-test'] : []) + \
  (config_all_devices.has_key('CONFIG_TPM_TIS_I2C') ? ['tpm-tis-i2c-test'] : []) + \
  (config_all_devices.has_key('CONFIG_VEXPRESS') ? ['test-arm-mptimer'] : []) + \
  (config_all_devices.has_key('CONFIG_MICROBIT') ? ['microbit-test'] : []) + \
  (config_all_devices.has_key('CONFIG_STM32L4X5_SOC') ? qtests_stm32l4x5 : []) + \
  (config_all_devices.has_key('CONFIG_FSI_APB2OPB_ASPEED') ? ['aspeed_fsi-test'] : []) + \
  (config_all_devices.has_key('CONFIG_STM32L4X5_SOC') and
   config_all_devices.has_key('CONFIG_DM163')? ['dm163-test'] : []) + \
  ['arm-cpu-features',
   'boot-serial-test']

# TODO: once aarch64 TCG is fixed on ARM 32 bit host, make bios-tables-test unconditional
qtests_aarch64 = \
  (cpu != 'arm' and unpack_edk2_blobs ? ['bios-tables-test'] : []) +                            \
  (config_all_accel.has_key('CONFIG_TCG') and config_all_devices.has_key('CONFIG_TPM_TIS_SYSBUS') ?            \
    ['tpm-tis-device-test', 'tpm-tis-device-swtpm-test'] : []) +                                         \
  (config_all_devices.has_key('CONFIG_XLNX_ZYNQMP_ARM') ? ['xlnx-can-test', 'fuzz-xlnx-dp-test'] : []) + \
  (config_all_devices.has_key('CONFIG_XLNX_VERSAL') ? ['xlnx-canfd-test', 'xlnx-versal-trng-test'] : []) + \
  (config_all_devices.has_key('CONFIG_RASPI') ? ['bcm2835-dma-test', 'bcm2835-i2c-test'] : []) +  \
  (config_all_accel.has_key('CONFIG_TCG') and                                            \
   config_all_devices.has_key('CONFIG_TPM_TIS_I2C') ? ['tpm-tis-i2c-test'] : []) + \
  (config_all_devices.has_key('CONFIG_ASPEED_SOC') ? qtests_aspeed64 : []) + \
  (config_all_devices.has_key('CONFIG_NPCM8XX') ? qtests_npcm8xx : []) + \
  qtests_cxl +                                                                                  \
  ['arm-cpu-features',
   'numa-test',
   'boot-serial-test',
   'migration-test']

qtests_s390x = \
  qtests_filter + \
  ['boot-serial-test',
   'drive_del-test',
   'device-plug-test',
   'virtio-ccw-test',
   'cpu-plug-test',
   'migration-test']

qtests_riscv32 = \
  (config_all_devices.has_key('CONFIG_SIFIVE_E_AON') ? ['sifive-e-aon-watchdog-test'] : [])

qtests_riscv64 = ['riscv-csr-test'] + \
  (unpack_edk2_blobs ? ['bios-tables-test'] : [])

qos_test_ss = ss.source_set()
qos_test_ss.add(
  'ac97-test.c',
  'adm1272-test.c',
  'adm1266-test.c',
  'ds1338-test.c',
  'e1000-test.c',
  'eepro100-test.c',
  'es1370-test.c',
  'lsm303dlhc-mag-test.c',
  'isl_pmbus_vr-test.c',
  'max34451-test.c',
  'megasas-test.c',
  'ne2000-test.c',
  'tulip-test.c',
  'nvme-test.c',
  'pca9552-test.c',
  'pci-test.c',
  'pcnet-test.c',
  'rs5c372-test.c',
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
  'virtio-iommu-test.c',
  'vmxnet3-test.c',
  'igb-test.c',
  'ufs-test.c',
  'riscv-iommu-test.c',
)

if config_all_devices.has_key('CONFIG_VIRTIO_SERIAL')
  qos_test_ss.add(files('virtio-serial-test.c'))
endif
if config_all_devices.has_key('CONFIG_IP_OCTAL_232')
  qos_test_ss.add(files('ipoctal232-test.c'))
endif

if host_os != 'windows'
  qos_test_ss.add(files('e1000e-test.c'))
endif
if have_virtfs
  qos_test_ss.add(files('virtio-9p-test.c'))
endif
if have_vhost_user
  qos_test_ss.add(files('vhost-user-test.c'))
endif
if have_tools and have_vhost_user_blk_server
  qos_test_ss.add(files('vhost-user-blk-test.c'))
endif

tpmemu_files = ['tpm-emu.c', 'tpm-util.c', 'tpm-tests.c']

migration_files = [files(
  'migration/bootfile.c',
  'migration/framework.c',
  'migration/migration-qmp.c',
  'migration/migration-util.c',
  'migration/compression-tests.c',
  'migration/cpr-tests.c',
  'migration/file-tests.c',
  'migration/misc-tests.c',
  'migration/precopy-tests.c',
  'migration/postcopy-tests.c',
)]

migration_tls_files = []
if gnutls.found()
  migration_tls_files = [files('migration/tls-tests.c',
                               '../unit/crypto-tls-psk-helpers.c'), gnutls]

  if tasn1.found()
    migration_tls_files += [files('../unit/crypto-tls-x509-helpers.c'), tasn1]
  endif
endif

qtests = {
  'aspeed_hace-test': files('aspeed-hace-utils.c', 'aspeed_hace-test.c'),
  'aspeed_smc-test': files('aspeed-smc-utils.c', 'aspeed_smc-test.c'),
  'ast2700-hace-test': files('aspeed-hace-utils.c', 'ast2700-hace-test.c'),
  'ast2700-smc-test': files('aspeed-smc-utils.c', 'ast2700-smc-test.c'),
  'bios-tables-test': [io, 'boot-sector.c', 'acpi-utils.c', 'tpm-emu.c'],
  'cdrom-test': files('boot-sector.c'),
  'dbus-vmstate-test': files('migration/migration-qmp.c',
                             'migration/migration-util.c') + dbus_vmstate1,
  'erst-test': files('erst-test.c'),
  'ivshmem-test': [rt, '../../contrib/ivshmem-server/ivshmem-server.c'],
  'migration-test': migration_files + migration_tls_files,
  'pxe-test': files('boot-sector.c'),
  'pnv-xive2-test': files('pnv-xive2-common.c', 'pnv-xive2-flush-sync.c',
                          'pnv-xive2-nvpg_bar.c'),
  'qos-test': [chardev, io, qos_test_ss.apply({}).sources()],
  'tpm-crb-swtpm-test': [io, tpmemu_files],
  'tpm-crb-test': [io, tpmemu_files],
  'tpm-tis-swtpm-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'tpm-tis-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'tpm-tis-i2c-test': [io, tpmemu_files, 'qtest_aspeed.c'],
  'tpm-tis-device-swtpm-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'tpm-tis-device-test': [io, tpmemu_files, 'tpm-tis-util.c'],
  'virtio-net-failover': migration_files,
  'vmgenid-test': files('boot-sector.c', 'acpi-utils.c'),
  'netdev-socket': files('netdev-socket.c', '../unit/socket-helpers.c'),
}

if vnc.found()
  gvnc = dependency('gvnc-1.0', method: 'pkg-config', required: false)
  if gvnc.found()
    qtests += {'vnc-display-test': [gvnc, keymap_targets]}
    qtests_generic += [ 'vnc-display-test' ]
  endif
endif

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
  has_kvm = ('CONFIG_KVM' in config_all_accel and host_os == 'linux'
             and cpu == target_base and fs.exists('/dev/kvm'))

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

  qtest_env.set('PYTHON', python.full_path())

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

    test_args = ['--tap', '-k']

    if test == 'migration-test' and has_kvm
      test_args += ['--full']
    endif

    test('qtest-@0@/@1@'.format(target_base, test),
         qtest_executables[test],
         depends: [test_deps, qtest_emulator, emulator_modules],
         env: qtest_env,
         args: test_args,
         protocol: 'tap',
         timeout: slow_qtests.get(test, 60),
         priority: slow_qtests.get(test, 60),
         suite: ['qtest', 'qtest-' + target_base])
  endforeach
endforeach
