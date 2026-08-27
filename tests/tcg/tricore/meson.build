tests = []

link_script = files('link.ld')[0]
crt0 = files('c'/'crt0-tc2x.S')
asmflags = ['-Wl,--mcpu=tc162',
            '-Wl,-T', link_script,
            '-mtc162',
            '-nostartfiles']
cflags = [asmflags, crt0]
qemu_args = ['-display', 'none',
             '-M', 'tricore_testboard',
             '-cpu', 'tc37x',
             '-kernel']

# Multi arch tests are not supported
# tests += tcg_tests['multiarch-softmmu']['tests']

setup_asm = {'cflags': asmflags, 'qemu_args': qemu_args}
setup_c = {'cflags': cflags, 'qemu_args': qemu_args}

tests += {
  'asm/test_abs.S': setup_asm,
  'asm/test_bmerge.S': setup_asm,
  'asm/test_clz.S': setup_asm,
  'asm/test_crcn.S': setup_asm,
  'asm/test_dextr.S': setup_asm,
  'asm/test_dvstep.S': setup_asm,
  'asm/test_fadd.S': setup_asm,
  'asm/test_fmul.S': setup_asm,
  'asm/test_ftohp.S': setup_asm,
  'asm/test_ftoi.S': setup_asm,
  'asm/test_ftou.S': setup_asm,
  'asm/test_hptof.S': setup_asm,
  'asm/test_imask.S': setup_asm,
  'asm/test_insert.S': setup_asm,
  'asm/test_ld_bu.S': setup_asm,
  'asm/test_ld_h.S': setup_asm,
  'asm/test_madd.S': setup_asm,
  'asm/test_msub.S': setup_asm,
  'asm/test_muls.S': setup_asm,
  'c/test_boot_to_main.c': setup_c,
  'c/test_context_save_areas.c': setup_c,
}

if 'qemu-system-tricore' in emulators
  tcg_tests += {
    'tricore-softmmu': {
      'cc': 'tricore-gcc',
      'cc_dockerfile': 'debian-tricore-cross',
      'cc_docker_host_arch': ['x86_64'],
      'folder': 'tricore',
      'gdb_arch': 'tricore',
      'qemu': emulators['qemu-system-tricore'],
      'tests': tests,
    }
  }
endif
