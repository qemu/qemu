if not have_tools or targetos == 'windows' or get_option('gprof')
  subdir_done()
endif

foreach cflag: config_host['QEMU_CFLAGS'].split()
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
