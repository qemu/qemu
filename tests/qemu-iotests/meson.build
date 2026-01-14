if not have_tools or host_os == 'windows'
  subdir_done()
endif

bash = find_program('bash', required: false, version: '>= 4.0')
if not bash.found()
  message('bash >= v4.0 not available ==> Disabled the qemu-iotests.')
  subdir_done()
endif

qemu_iotests_binaries = [qemu_img, qemu_io, qemu_nbd, qsd]
qemu_iotests_env = {'PYTHON': python.full_path()}
# If altering this definition, also update docs/devel/testing/main.rst
# section on 'check-block' targets to reflect the changes
qemu_iotests_drivers = {
  'qcow2': 'quick',
  'raw': 'slow',
  'luks': 'thorough',
  'nbd': 'thorough',
  'parallels': 'thorough',
  'qed': 'thorough',
  'vdi': 'thorough',
  'vhdx': 'thorough',
  'vmdk': 'thorough',
  'vpc': 'thorough',
}

foreach k, v : emulators
  if k.startswith('qemu-system-')
    qemu_iotests_binaries += v
  endif
endforeach

qemu_iotests_check_cmd = files('check')

foreach driver, speed: qemu_iotests_drivers
  # Drivers tagged 'quick' get the subset of tests in the 'auto'
  # group, run by default with 'make check' / 'make check-block'
  seen = []
  if speed == 'quick'
    args = ['-tap', '-' + driver, '-g', 'auto']
    suites = ['block']

    rc = run_command(
      [python, qemu_iotests_check_cmd] + args + ['-n'],
      check: true,
    )

    foreach item: rc.stdout().strip().split()
      seen += item
      args = [qemu_iotests_check_cmd,
              '-tap', '-' + driver, item,
              '--source-dir', meson.current_source_dir(),
              '--build-dir', meson.current_build_dir()]
      # Some individual tests take as long as 45 seconds
      # Bump the timeout to 3 minutes for some headroom
      # on slow machines to minimize spurious failures
      test('io-' + driver + '-' + item,
           python,
           args: args,
           depends: qemu_iotests_binaries,
           env: qemu_iotests_env,
           protocol: 'tap',
           timeout: 180,
           suite: suites)
    endforeach
  endif

  # Every driver gets put in the driver specific suite
  suites = ['block-' + driver + '-optional']
  # Any driver tagged quick or slow also gets added to slow
  # otherwise its tagged thorough
  if speed != 'thorough'
    suites += ['block-slow']
  else
    suites += ['block-thorough']
  endif

  args = ['-tap', '-' + driver]

  rc = run_command(
      [python, qemu_iotests_check_cmd] + args + ['-n'],
      check: true,
  )

  foreach item: rc.stdout().strip().split()
      # Skip any tests already added from the 'auto' group
      # as they're run in the 'quick' suite already
      if item in seen
          continue
      endif

      args = [qemu_iotests_check_cmd,
              '-tap', '-' + driver, item,
              '--source-dir', meson.current_source_dir(),
              '--build-dir', meson.current_build_dir()]
      # Some individual tests take as long as 45 seconds
      # Bump the timeout to 3 minutes for some headroom
      # on slow machines to minimize spurious failures
      test('io-' + driver + '-' + item,
           python,
           args: args,
           depends: qemu_iotests_binaries,
           env: qemu_iotests_env,
           protocol: 'tap',
           timeout: 180,
           suite: suites)
  endforeach
endforeach
