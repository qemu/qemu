subdir('system')

tests = []

foreach t: tcg_tests['multiarch-linux-user']['tests']
  foreach src, setup: t
    if 'gdb_test' in setup
      setup += {'gdb_feat': 'loongarch64'}
    endif
    tests += {src: setup}
  endforeach
endforeach

tests += {
  'test_bit.c': {},
  'test_fclass.c': {},
  'test_fcsr.c': {},
  'test_fpcom.c': {},
  'test_div.c': {},
  'test_pcadd.c': {},
}

if 'qemu-loongarch64' in emulators
  tcg_tests += {
    'loongarch64-linux-user': {
      'cc': 'loongarch64-unknown-linux-gnu-gcc',
      'cc_dockerfile': 'debian-loongarch-cross',
      'folder': 'loongarch64',
      'gdb_arch': 'loongarch64',
      'gdb_feat_version': {
        # previous version report "Architecture rejected target-supplied
        # description", exposing a mismatch between our gdbstub and gdb.
        'loongarch64': '16.1',
      },
      'qemu': emulators['qemu-loongarch64'],
      'tests': tests,
    }
  }
endif
