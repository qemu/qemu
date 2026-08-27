tests = []

# Multi arch tests are not supported
# tests += tcg_tests['multiarch-softmmu']['tests']

link_script = files('../semihost.ld')[0]
cflags = ['-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-Wl,-T', link_script]
qemu_args = ['-M', 'virt',
             '-display', 'none',
             '-serial', 'stdio',
             '-semihosting',
             '-bios']

setup = {'cflags': cflags, 'qemu_args': qemu_args}

tests += {
  'doubletrap.S': setup + {'plugin_test': {'plugin': 'discons'}},
  'interruptedmemory.S': setup + {'plugin_test': {'plugin': 'discons'}},
  'issue1060.S': setup,
  'test-mepc-masking.S': setup,
  'test-crc32.S': setup + {'qemu_args': ['-cpu', 'rv64,xlrbr=true', qemu_args]},
  'test-minstret-ecall.S': setup + {'qemu_args': ['-icount', 'shift=1', qemu_args]},
  'test-misa-w.S': setup + {'qemu_args': ['-cpu', 'rv64,x-misa-w=true,c=true,v=true', qemu_args]},
}

tests += {
  'test-rv128-mstatus.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'x-rv128', qemu_args],
  }
}

tests += {
  'test-rv128-sstatus.S': {
    'cflags': cflags,
    'qemu_args': ['-cpu', 'x-rv128', qemu_args],
  }
}

tests += {
  'test-vle32ff.S': {
    'cflags': cflags + ['-march=rv64gcv'],
    'qemu_args': ['-cpu', 'rv64,v=true', qemu_args],
  }
}

tests += {
  'test-zicclsm.S': {
    'cflags': cflags + ['-march=rv64gcv_zfh'],
    'qemu_args': ['-cpu', 'rv64,v=true,zfh=true,zicclsm=true', qemu_args],
  }
}
tests += {
  'test-zicclsm.S': {
    'exe_name': 'test-zicclsm-off',
    'cflags': cflags + ['-march=rv64gcv_zfh', '-DZICCLSM_DISABLED'],
    'qemu_args': ['-cpu', 'rv64,v=true,zfh=true,zicclsm=false', qemu_args],
  }
}

if 'qemu-system-riscv64' in emulators
  tcg_tests += {
    'riscv64-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'cc_docker_host_arch': cc_docker_host_arch,
      'folder': 'riscv64',
      'gdb_arch': 'riscv64',
      'qemu': emulators['qemu-system-riscv64'],
      'tests': tests,
    }
  }
endif
