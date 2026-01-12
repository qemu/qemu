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
qemu_iotests_formats = {
  'qcow2': 'quick',
  'raw': 'slow',
  'parallels': 'thorough',
  'qed': 'thorough',
  'vdi': 'thorough',
  'vhdx': 'thorough',
  'vmdk': 'thorough',
  'vpc': 'thorough'
}

foreach k, v : emulators
  if k.startswith('qemu-system-')
    qemu_iotests_binaries += v
  endif
endforeach

qemu_iotests_check_cmd = files('check')

foreach format, speed: qemu_iotests_formats
  # Formats tagged 'quick' get the subset of tests in the 'auto'
  # group, run by default with 'make check' / 'make check-block'
  seen = []
  if speed == 'quick'
    args = ['-tap', '-' + format, '-g', 'auto']
    suites = ['block']

    rc = run_command(
      [python, qemu_iotests_check_cmd] + args + ['-n'],
      check: true,
    )

    foreach item: rc.stdout().strip().split()
      seen += item
      args = [qemu_iotests_check_cmd,
              '-tap', '-' + format, item,
              '--source-dir', meson.current_source_dir(),
              '--build-dir', meson.current_build_dir()]
      # Some individual tests take as long as 45 seconds
      # Bump the timeout to 3 minutes for some headroom
      # on slow machines to minimize spurious failures
      test('io-' + format + '-' + item,
           python,
           args: args,
           depends: qemu_iotests_binaries,
           env: qemu_iotests_env,
           protocol: 'tap',
           timeout: 180,
           suite: suites)
    endforeach
  endif

  suites = []
  # Any format tagged quick or slow also gets added to slow
  # otherwise its tagged thorough
  if speed != 'thorough'
    suites += ['block-slow']
  else
    suites += ['block-thorough']
  endif

  args = ['-tap', '-' + format]

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
              '-tap', '-' + format, item,
              '--source-dir', meson.current_source_dir(),
              '--build-dir', meson.current_build_dir()]
      # Some individual tests take as long as 45 seconds
      # Bump the timeout to 3 minutes for some headroom
      # on slow machines to minimize spurious failures
      test('io-' + format + '-' + item,
           python,
           args: args,
           depends: qemu_iotests_binaries,
           env: qemu_iotests_env,
           protocol: 'tap',
           timeout: 180,
           suite: suites)
  endforeach
endforeach
