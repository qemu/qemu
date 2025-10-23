dtbs = [
  'bamboo.dtb',
  'canyonlands.dtb',
  'pegasos1.dtb',
  'pegasos2.dtb',
  'petalogix-ml605.dtb',
  'petalogix-s3adsp1800.dtb',
]

dtc = find_program('dtc', required: false)
if dtc.found()
  foreach out : dtbs
    f = fs.replace_suffix(out, '.dts')
    custom_target(out,
        build_by_default: have_system,
        input: files(f),
        output: out,
        install: get_option('install_blobs'),
        install_dir: qemu_datadir / 'dtb',
        command: [ dtc, '-q', '-I', 'dts', '-O', 'dtb',
                        '-o', '@OUTPUT@', '@INPUT0@' ])
  endforeach
else
    install_data(dtbs, install_dir: qemu_datadir / 'dtb')
endif
