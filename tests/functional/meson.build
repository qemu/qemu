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
}

tests_generic_system = [
  'empty_cpu_model',
  'info_usernet',
  'version',
]

tests_ppc_quick = [
  'ppc_74xx',
]

tests_generic_linuxuser = [
]

tests_generic_bsduser = [
]

tests_x86_64_system_quick = [
  'cpu_queries',
  'mem_addr_space',
  'pc_cpu_hotplug_props',
  'virtio_version',
]

tests_x86_64_system_thorough = [
]

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
      test('func-@0@/@1@'.format(target_base, test),
           python,
           depends: [test_deps, test_emulator, emulator_modules],
           env: test_env,
           args: [meson.current_source_dir() / 'test_' + test + '.py'],
           protocol: 'tap',
           timeout: test_timeouts.get(test, 60),
           priority: test_timeouts.get(test, 60),
           suite: suites)
    endforeach
  endforeach
endforeach
