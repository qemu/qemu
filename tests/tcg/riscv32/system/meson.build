# SPDX-License-Identifier: GPL-2.0-or-later

tests = []

link_script = files('../semihost.ld')[0]
cflags = ['-march=rv32im_zicsr',
          '-mabi=ilp32',
          '-nostdlib',
          '-ffreestanding',
          '-Wa,--noexecstack',
          '-Wl,-T', link_script]
qemu_args = ['-M', 'virt',
             '-display', 'none',
             '-serial', 'stdio',
             '-semihosting',
             '-bios']

tests += {
  'test-mcycle.S': {
    'cflags': cflags,
    'qemu_args': ['-icount', 'shift=1', qemu_args],
  },
}

if 'qemu-system-riscv32' in emulators
  tcg_tests += {
    'riscv32-softmmu': {
      'cc': cc,
      'cc_dockerfile': cc_dockerfile,
      'folder': 'riscv32',
      'gdb_arch': 'riscv32',
      'qemu': emulators['qemu-system-riscv32'],
      'tests': tests,
    }
  }
endif
