# QEMU functional tests:
# Tests that are put in the 'quick' category are run by default during
# 'make check'. Everything that should not be run during 'make check'
# (e.g. tests that fetch assets from the internet) should be put into
# the 'thorough' category instead.

# Most tests run too slow with TCI enabled, so skip the functional tests there
if get_option('tcg_interpreter')
  subdir_done()
endif

subdir('aarch64')
subdir('alpha')
subdir('arm')
subdir('avr')
subdir('hppa')

test_mips_timeouts = {
  'mips_malta' : 480,
}

test_mipsel_timeouts = {
  'mipsel_malta' : 420,
  'mipsel_replay' : 480,
}

test_mips64_timeouts = {
  'mips64_malta' : 240,
}

test_mips64el_timeouts = {
  'mips64el_malta' : 420,
  'mips64el_replay' : 180,
}

test_ppc_timeouts = {
  'ppc_40p' : 240,
}

test_ppc64_timeouts = {
  'ppc64_hv' : 1000,
  'ppc64_powernv' : 480,
  'ppc64_pseries' : 480,
  'ppc64_replay' : 210,
  'ppc64_tuxrun' : 420,
  'ppc64_mac99' : 120,
}

test_riscv64_timeouts = {
  'riscv64_tuxrun' : 120,
}

test_s390x_timeouts = {
  's390x_ccw_virtio' : 420,
}

test_sh4_timeouts = {
  'sh4_tuxrun' : 240,
}

test_x86_64_timeouts = {
  'acpi_bits' : 420,
  'intel_iommu': 300,
  'netdev_ethtool' : 180,
  'virtio_balloon': 120,
  'x86_64_kvm_xen' : 180,
  'x86_64_replay' : 480,
}

tests_generic_system = [
  'empty_cpu_model',
  'info_usernet',
  'version',
]

tests_generic_linuxuser = [
]

tests_generic_bsduser = [
]

tests_i386_system_quick = [
  'i386_migration',
]

tests_i386_system_thorough = [
  'i386_replay',
  'i386_tuxrun',
]

tests_loongarch64_system_thorough = [
  'loongarch64_virt',
]

tests_m68k_system_thorough = [
  'm68k_mcf5208evb',
  'm68k_nextcube',
  'm68k_replay',
  'm68k_q800',
  'm68k_tuxrun',
]

tests_microblaze_system_thorough = [
  'microblaze_replay',
  'microblaze_s3adsp1800'
]

tests_microblazeel_system_thorough = [
  'microblazeel_s3adsp1800'
]

tests_mips_system_thorough = [
  'mips_malta',
  'mips_replay',
  'mips_tuxrun',
]

tests_mipsel_system_thorough = [
  'mipsel_malta',
  'mipsel_replay',
  'mipsel_tuxrun',
]

tests_mips64_system_thorough = [
  'mips64_malta',
  'mips64_tuxrun',
]

tests_mips64el_system_thorough = [
  'mips64el_fuloong2e',
  'mips64el_loongson3v',
  'mips64el_malta',
  'mips64el_replay',
  'mips64el_tuxrun',
]

tests_or1k_system_thorough = [
  'or1k_replay',
  'or1k_sim',
]

tests_ppc_system_quick = [
  'ppc_migration',
  'ppc_74xx',
]

tests_ppc_system_thorough = [
  'ppc_40p',
  'ppc_amiga',
  'ppc_bamboo',
  'ppc_mac',
  'ppc_mpc8544ds',
  'ppc_replay',
  'ppc_sam460ex',
  'ppc_tuxrun',
  'ppc_virtex_ml507',
]

tests_ppc64_system_quick = [
  'ppc64_migration',
]

tests_ppc64_system_thorough = [
  'ppc64_e500',
  'ppc64_hv',
  'ppc64_powernv',
  'ppc64_pseries',
  'ppc64_replay',
  'ppc64_reverse_debug',
  'ppc64_tuxrun',
  'ppc64_mac99',
]

tests_riscv32_system_quick = [
  'riscv32_migration',
  'riscv_opensbi',
]

tests_riscv32_system_thorough = [
  'riscv32_tuxrun',
]

tests_riscv64_system_quick = [
  'riscv64_migration',
  'riscv_opensbi',
]

tests_riscv64_system_thorough = [
  'riscv64_sifive_u',
  'riscv64_tuxrun',
]

tests_rx_system_thorough = [
  'rx_gdbsim',
]

tests_s390x_system_thorough = [
  's390x_ccw_virtio',
  's390x_pxelinux',
  's390x_replay',
  's390x_topology',
  's390x_tuxrun',
]

tests_sh4_system_thorough = [
  'sh4_r2d',
  'sh4_tuxrun',
]

tests_sh4eb_system_thorough = [
  'sh4eb_r2d',
]

tests_sparc_system_quick = [
  'sparc_migration',
]

tests_sparc_system_thorough = [
  'sparc_replay',
  'sparc_sun4m',
]

tests_sparc64_system_quick = [
  'sparc64_migration',
]

tests_sparc64_system_thorough = [
  'sparc64_sun4u',
  'sparc64_tuxrun',
]

tests_x86_64_system_quick = [
  'cpu_queries',
  'mem_addr_space',
  'x86_64_migration',
  'pc_cpu_hotplug_props',
  'virtio_version',
  'x86_cpu_model_versions',
  'vnc',
  'memlock',
]

tests_x86_64_system_thorough = [
  'acpi_bits',
  'intel_iommu',
  'linux_initrd',
  'x86_64_multiprocess',
  'netdev_ethtool',
  'virtio_balloon',
  'virtio_gpu',
  'x86_64_hotplug_blk',
  'x86_64_hotplug_cpu',
  'x86_64_kvm_xen',
  'x86_64_replay',
  'x86_64_reverse_debug',
  'x86_64_tuxrun',
]

tests_xtensa_system_thorough = [
  'xtensa_lx60',
  'xtensa_replay',
]

precache_all = []
foreach speed : ['quick', 'thorough']
  foreach dir : target_dirs

    target_base = dir.split('-')[0]

    if dir.endswith('-softmmu')
      sysmode = 'system'
      test_emulator = emulators['qemu-system-' + target_base]
    elif dir.endswith('-linux-user')
      sysmode = 'linuxuser'
      test_emulator = emulators['qemu-' + target_base]
    elif dir.endswith('-bsd-user')
      sysmode = 'bsduser'
      test_emulator = emulators['qemu-' + target_base]
    else
      continue
    endif

    if speed == 'quick'
      suites = ['func-quick', 'func-' + target_base]
      target_tests = get_variable('tests_' + target_base + '_' + sysmode + '_quick', []) \
                     + get_variable('tests_generic_' + sysmode)
    else
      suites = ['func-' + speed, 'func-' + target_base + '-' + speed, speed]
      target_tests = get_variable('tests_' + target_base + '_' + sysmode + '_' + speed, [])
    endif

    test_deps = [roms, keymap_targets]
    test_env = environment()
    if have_tools
      test_env.set('QEMU_TEST_QEMU_IMG', meson.global_build_root() / 'qemu-img')
      test_deps += [qemu_img]
    endif
    test_env.set('QEMU_TEST_QEMU_BINARY', test_emulator.full_path())
    test_env.set('QEMU_BUILD_ROOT', meson.project_build_root())
    test_env.set('PYTHONPATH', meson.project_source_root() / 'python:' +
                               meson.current_source_dir())

    foreach test : target_tests
      testname = '@0@-@1@'.format(target_base, test)
      if fs.exists('test_' + test + '.py')
        testfile = 'test_' + test + '.py'
      elif fs.exists('generic' / 'test_' + test + '.py')
        testfile = 'generic' / 'test_' + test + '.py'
      else
        testfile = target_base / 'test_' + test + '.py'
      endif
      testpath = meson.current_source_dir() / testfile
      teststamp = testname + '.tstamp'
      test_precache_env = environment()
      test_precache_env.set('QEMU_TEST_PRECACHE', meson.current_build_dir() / teststamp)
      test_precache_env.set('PYTHONPATH', meson.project_source_root() / 'python:' +
                                          meson.current_source_dir())
      precache = custom_target('func-precache-' + testname,
                               output: teststamp,
                               command: [python, testpath],
                               depend_files: files(testpath),
                               build_by_default: false,
                               env: test_precache_env)
      precache_all += precache
      if is_variable('test_' + target_base + '_timeouts')
        time_out = get_variable('test_' + target_base + '_timeouts').get(test, 90)
      else
        time_out = 90
      endif

      # Ideally we would add 'precache' to 'depends' here, such that
      # 'build_by_default: false' lets the pre-caching automatically
      # run immediately before the test runs. In practice this is
      # broken in meson, with it running the pre-caching in the normal
      # compile phase https://github.com/mesonbuild/meson/issues/2518
      # If the above bug ever gets fixed, when QEMU changes the min
      # meson version, add the 'depends' and remove the custom
      # 'run_target' logic below & in Makefile.include
      test('func-' + testname,
           python,
           depends: [test_deps, test_emulator, emulator_modules, plugin_modules],
           env: test_env,
           args: [testpath],
           protocol: 'tap',
           timeout: time_out,
           priority: time_out,
           suite: suites)
    endforeach
  endforeach
endforeach

run_target('precache-functional',
           depends: precache_all,
           command: [python, '-c', ''])
