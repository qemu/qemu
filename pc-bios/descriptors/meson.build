if install_edk2_blobs
  foreach f: [
    '50-edk2-i386-secure.json',
    '50-edk2-x86_64-secure.json',
    '60-edk2-aarch64.json',
    '60-edk2-arm.json',
    '60-edk2-i386.json',
    '60-edk2-x86_64.json'
  ]
    configure_file(input: files(f),
                   output: f,
                   configuration: {'DATADIR': get_option('prefix') / qemu_datadir},
                   install: get_option('install_blobs'),
                   install_dir: qemu_datadir / 'firmware')
  endforeach
endif
