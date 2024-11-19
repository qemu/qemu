# QEMU functional tests:
# Tests that are put in the 'quick' category are run by default during
# 'make check'. Everything that should not be run during 'make check'
# (e.g. tests that fetch assets from the internet) should be put into
# the 'thorough' category instead.

# Most tests run too slow with TCI enabled, so skip the functional tests there
if get_option('tcg_interpreter')
  subdir_done()
endif

# Timeouts for individual tests that can be slow e.g. with debugging enabled
test_timeouts = {
  'aarch64_raspi4' : 480,
  'aarch64_sbsaref_alpine' : 720,
  'aarch64_sbsaref_freebsd' : 720,
  'aarch64_virt' : 720,
  'acpi_bits' : 420,
  'arm_aspeed' : 600,
  'arm_bpim2u' : 500,
  'arm_collie' : 180,
  'arm_orangepi' : 540,
  'arm_raspi2' : 120,
  'arm_tuxrun' : 240,
  'arm_sx1' : 360,
  'mips_malta' : 120,
  'netdev_ethtool' : 180,
  'ppc_40p' : 240,
  'ppc64_hv' : 1000,
  'ppc64_powernv' : 480,
  'ppc64_pseries' : 480,
  'ppc64_tuxrun' : 420,
  'riscv64_tuxrun' : 120,
  's390x_ccw_virtio' : 420,
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

tests_aarch64_system_thorough = [
  'aarch64_raspi3',
  'aarch64_raspi4',
  'aarch64_sbsaref',
  'aarch64_sbsaref_alpine',
  'aarch64_sbsaref_freebsd',
  'aarch64_virt',
  'multiprocess',
]

tests_alpha_system_thorough = [
  'alpha_clipper',
]

tests_arm_system_thorough = [
  'arm_aspeed',
  'arm_bpim2u',
  'arm_canona1100',
  'arm_collie',
  'arm_integratorcp',
  'arm_orangepi',
  'arm_raspi2',
  'arm_sx1',
  'arm_vexpress',
  'arm_tuxrun',
]

tests_arm_linuxuser_thorough = [
  'arm_bflt',
]

tests_avr_system_thorough = [
  'avr_mega2560',
]

tests_i386_system_thorough = [
  'i386_tuxrun',
]

tests_loongarch64_system_thorough = [
  'loongarch64_virt',
]

tests_m68k_system_thorough = [
  'm68k_mcf5208evb',
  'm68k_nextcube',
  'm68k_q800',
]

tests_microblaze_system_thorough = [
  'microblaze_s3adsp1800'
]

tests_microblazeel_system_thorough = [
  'microblazeel_s3adsp1800'
]

tests_mips_system_thorough = [
  'mips_malta',
  'mips_tuxrun',
]

tests_mipsel_system_thorough = [
  'mipsel_malta',
  'mipsel_tuxrun',
]

tests_mips64_system_thorough = [
  'mips64_tuxrun',
]

tests_mips64el_system_thorough = [
  'mips64el_fuloong2e',
  'mips64el_loongson3v',
  'mips64el_malta',
  'mips64el_tuxrun',
]

tests_or1k_system_thorough = [
  'or1k_sim',
]

tests_ppc_system_quick = [
  'ppc_74xx',
]

tests_ppc_system_thorough = [
  'ppc_405',
  'ppc_40p',
  'ppc_amiga',
  'ppc_bamboo',
  'ppc_mac',
  'ppc_mpc8544ds',
  'ppc_tuxrun',
  'ppc_virtex_ml507',
]

tests_ppc64_system_thorough = [
  'ppc64_e500',
  'ppc64_hv',
  'ppc64_powernv',
  'ppc64_pseries',
  'ppc64_tuxrun',
]

tests_riscv32_system_quick = [
  'riscv_opensbi',
]

tests_riscv32_system_thorough = [
  'riscv32_tuxrun',
]

tests_riscv64_system_quick = [
  'riscv_opensbi',
]

tests_riscv64_system_thorough = [
  'riscv64_tuxrun',
]

tests_rx_system_thorough = [
  'rx_gdbsim',
]

tests_s390x_system_thorough = [
  's390x_ccw_virtio',
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

tests_sparc_system_thorough = [
  'sparc_sun4m',
]

tests_sparc64_system_thorough = [
  'sparc64_sun4u',
  'sparc64_tuxrun',
]

tests_x86_64_system_quick = [
  'cpu_queries',
  'mem_addr_space',
  'pc_cpu_hotplug_props',
  'virtio_version',
  'x86_cpu_model_versions',
]

tests_x86_64_system_thorough = [
  'acpi_bits',
  'x86_64_tuxrun',
  'linux_initrd',
  'multiprocess',
  'netdev_ethtool',
  'virtio_gpu',
]

tests_xtensa_system_thorough = [
  'xtensa_lx60',
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

    test_deps = roms
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
      testfile = 'test_' + test + '.py'
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
           depends: [test_deps, test_emulator, emulator_modules],
           env: test_env,
           args: [testpath],
           protocol: 'tap',
           timeout: test_timeouts.get(test, 90),
           priority: test_timeouts.get(test, 90),
           suite: suites)
    endforeach
  endforeach
endforeach

run_target('precache-functional',
           depends: precache_all,
           command: ['true'])
