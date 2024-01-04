if not have_tools or host_os == 'windows'
  subdir_done()
endif

foreach cflag: qemu_ldflags
  if cflag.startswith('-fsanitize') and \
     not cflag.contains('safe-stack') and not cflag.contains('cfi-icall')
    message('Sanitizers are enabled ==> Disabled the qemu-iotests.')
    subdir_done()
  endif
endforeach

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
  'qed': 'thorough',
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
  if speed == 'quick'
    suites = 'block'
  else
    suites = ['block-' + speed, speed]
  endif

  args = ['-tap', '-' + format]
  if speed == 'quick'
      args += ['-g', 'auto']
  endif

  rc = run_command(
      [python, qemu_iotests_check_cmd] + args + ['-n'],
      check: true,
  )

  foreach item: rc.stdout().strip().split()
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
