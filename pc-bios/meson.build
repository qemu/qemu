bzip2 = find_program('bzip2')

install_blobs = 'INSTALL_BLOBS' in config_host
if 'DECOMPRESS_EDK2_BLOBS' in config_host
  fds = [
    'edk2-aarch64-code.fd',
    'edk2-arm-code.fd',
    'edk2-arm-vars.fd',
    'edk2-i386-code.fd',
    'edk2-i386-secure-code.fd',
    'edk2-i386-vars.fd',
    'edk2-x86_64-code.fd',
    'edk2-x86_64-secure-code.fd',
  ]

  foreach f : fds
    custom_target(f,
                  output: f,
                  input: '@0@.bz2'.format(f),
                  capture: true,
                  install: install_blobs,
                  install_dir: config_host['qemu_datadir'],
                  command: [ bzip2, '-dc', '@INPUT0@' ])
  endforeach
endif

subdir('descriptors')
