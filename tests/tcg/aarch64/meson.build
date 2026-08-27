cc = 'aarch64-linux-gnu-gcc'
cc_dockerfile = 'debian-all-test-cross'
cc_docker_host_arch = ['aarch64', 'x86_64']

cc_feat_cflags = {
  'aes': '-march=armv8-a+aes',
  'sve': '-march=armv8.1-a+sve',
  'sve2': '-march=armv8.1-a+sve2',
  'v8.2': '-march=armv8.2-a',
  'v8.3': '-march=armv8.3-a',
  'v8.5': '-march=armv8.5-a',
  'bti':  '-mbranch-protection=standard',
  'mte':  '-march=armv8.5-a+memtag',
  'sme':  '-Wa,-march=armv9-a+sme',
  'fprcvt': '-march=armv9-a+fprcvt',
}

gdb_feat_version = {
  'sme-tiles': '14.1',
  'mte': '15.1',
  'mte-baremetal': '16.0',
}

tests = []

# Multi arch tests
tests += tcg_tests['multiarch-linux-user']['tests']

# Base architecture tests
tests += {
  '../arm/fcvt.c': {'cflags': '-lm', 'expected_output': 'fcvt.ref'},
  'pcalign-a64.c': {},
  'lse2-fault.c': {},
  'test-2248.c': {},
  'test-2150.c': {},
}

tests += {'dcpop.c': {'cc_feat': 'v8.2'}}
tests += {'dcpodp.c': {'cc_feat': 'v8.5'}}

# Pauth Tests
cpu_max = ['-cpu', 'max']
cpu_neoverse = ['-cpu', 'neoverse-v1']
tests += {
  'test-2375.c': {'cc_feat': 'v8.3'},
  'pauth-1.c': {'cc_feat': 'v8.3', 'qemu_args': cpu_max},
  'pauth-2.c': {'cc_feat': 'v8.3', 'qemu_args': cpu_max},
  # Choose a cpu with FEAT_Pauth but without FEAT_FPAC for pauth-[45].
  'pauth-4.c': {'cc_feat': 'v8.3', 'qemu_args': cpu_neoverse},
  'pauth-5.c': {'cc_feat': 'v8.3', 'qemu_args': cpu_neoverse},
}

# BTI Tests
# bti-1 tests the elf notes, so we require special compiler support.
f = ['-fno-stack-protector', '-nostdlib']
tests += {
  'bti-1.c': {'cc_feat': 'bti', 'cflags': f},
  'bti-3.c': {'cc_feat': 'bti', 'cflags': f},
}
# bti-2 tests PROT_BTI, so no special compiler support required.
tests += {'bti-2.c': {}}

# MTE Tests
tests += {
  'mte-1.c': {'cc_feat': 'mte'},
  'mte-2.c': {'cc_feat': 'mte'},
  'mte-3.c': {'cc_feat': 'mte'},
  'mte-4.c': {'cc_feat': 'mte'},
  'mte-5.c': {'cc_feat': 'mte'},
  'mte-6.c': {'cc_feat': 'mte'},
  'mte-7.c': {'cc_feat': 'mte'},
  'mte-8.c': {'cc_feat': 'mte'},
  'mte-9.c': {'cc_feat': 'mte'},
  'mte-10.c': {'cc_feat': 'mte'},
}
tests += {
  'mte-8.c': {
    'gdb_test': ['--test', files('gdbstub/test-mte.py'), '--', '--mode=user'],
    'gdb_feat': 'mte',
  }
}

# SME Tests
tests += {
  'sme-outprod1.c': {'cc_feat': 'sme'},
  'sme-smopa-1.c': {'cc_feat': 'sme'},
  'sme-smopa-2.c': {'cc_feat': 'sme'},
  'sme-fmopa-1.c': {'cc_feat': 'sme'},
  'sme-fmopa-2.c': {'cc_feat': 'sme'},
  'sme-fmopa-3.c': {'cc_feat': 'sme'},
}
sysregs_sme = 'sysregs-sme'
tests += {
  'sysregs.c': {
    'exe_name': sysregs_sme,
    'cc_feat': 'sme',
    'gdb_test': ['--test', files('gdbstub/test-sme.py'), '--',
                 'test_sme', '--gdb_basic_za_test'],
  }
}
tests += {
  'sysregs.c': {
    'test_name': 'sysregs-sme-tile-slice',
    'exe_name': sysregs_sme,
    'gdb_test': ['--test', files('gdbstub/test-sme.py'), '--',
                 'test_sme', '--gdb_tile_slice_test'],
    'gdb_feat': 'sme-tiles',
  }
}
tests += {
  'sysregs.c': {
    'test_name': 'sysregs-sme2',
    'exe_name': sysregs_sme,
    'gdb_test': ['--test', files('gdbstub/test-sme2.py')],
  }
}

# GCS Tests
tests += {
  'gcsstr.c': {},
  'gcspushm.c': {},
  'gcsss.c': {},
}

# System Registers Tests
tests += {'sysregs.c': {}}

# Crypto Tests
tests += {'test-aes.c': {'cc_feat': 'aes'}}

# Vector SHA1
# Work around compiler false-positive warning, as we do for the 'sha1' test
tests += {'../multiarch/sha1.c': {
            'exe_name': 'sha1-vector',
            'cflags': ['-O3', '-Wno-stringop-overread'],
            'expected_output': '../multiarch/sha1.ref',
         }}

# Vector versions of sha512 (-O3 triggers vectorisation)
tests += {'../multiarch/sha512.c': {
            'exe_name': 'sha512-vector',
            'cflags': ['-O3'],
         }}

# SVE Tests
tests += {
  'sve-ioctls.c': {'cc_feat': 'sve'},
  '../multiarch/sha512.c': {
     'exe_name': 'sha512-sve',
     'cc_feat': 'sve',
  },
  'sve-str.c': {'cc_feat': 'sve', 'cflags': ['-O1']},
}
tests += {
  'sysregs.c': {
    'exe_name': 'sysregs-sve',
    'cc_feat': 'sve',
    'gdb_test': ['--test', files('gdbstub/test-sve.py')],
  },
  'sve-ioctls.c': {
    'gdb_test': ['--test', files('gdbstub/test-sve-ioctl.py')],
  },
}

# SVE2 Tests
tests += {
  'test-826.c': {'cc_feat': 'sve2'},
  'sve-while-ptr.c': {'cc_feat': 'sve2'},
}

# FPRCVT test
tests += {
  '../arm/fcvt.c': {
    'exe_name': 'fcvt-fprcvt',
    'cflags': ['-DFPRCVT', '-lm'],
    # fprcvt is available only from gcc-16.
    # Use raw opcode until it's available in cross container.
    # 'cc_feat': 'fprcvt',
    'expected_output': 'fcvt.ref'},
}

# Semihosting tests
# Add -I path for semicall.h
semihosting_cflags = ['-I', meson.current_source_dir()]

tests += {
  '../multiarch/arm-compat-semi/semiconsole.c': {
    'cflags': semihosting_cflags,
    'wrapper': [prog_run_with_input, 'X'],
  },
  '../multiarch/arm-compat-semi/semihosting.c': {'cflags': semihosting_cflags},
}

if 'qemu-aarch64' in emulators
  tcg_tests += {
    'aarch64-linux-user': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'cc_feat_cflags': cc_feat_cflags,
      'folder': 'aarch64',
      'gdb_arch': 'aarch64',
      'gdb_feat_version': gdb_feat_version,
      'qemu': emulators['qemu-aarch64'],
      'tests': tests,
    }
  }
endif
