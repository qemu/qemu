foreach s: [16, 24, 32, 48, 64, 128, 256, 512]
  s = '@0@x@0@'.format(s.to_string())
  install_data('qemu_@0@.png'.format(s),
               rename: 'qemu.png',
               install_dir: config_host['qemu_icondir'] / 'hicolor' / s / 'apps')
endforeach

install_data('qemu_32x32.bmp',
             rename: 'qemu.bmp',
             install_dir: config_host['qemu_icondir'] / 'hicolor' / '32x32' / 'apps')

install_data('qemu.svg',
             install_dir: config_host['qemu_icondir'] / 'hicolor' / 'scalable' / 'apps')
