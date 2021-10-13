if have_tools and targetos != 'windows' and not get_option('gprof')
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
  foreach format, speed: qemu_iotests_formats
    if speed == 'quick'
      suites = 'block'
    else
      suites = ['block-' + speed, speed]
    endif
    test('qemu-iotests ' + format, sh, args: [files('../check-block.sh'), format],
         depends: qemu_iotests_binaries, env: qemu_iotests_env,
         protocol: 'tap',
         suite: suites,
         timeout: 0,
         is_parallel: false)
  endforeach
endif
