# SPDX-License-Identifier: GPL-2.0-or-later

tests = []

cflags = ['-nostdlib', '-ffreestanding',
          '-march=r5900', '-msoft-float',
          '-mno-abicalls', '-fno-pic', '-G0',
          '-I', meson.current_source_dir() / 'include',
          files('boot.S')[0], files('harness.c')[0],
          '-Wl,-T', files('kernel.ld')[0],
          '-Wl,--no-warn-rwx-segments', '-Wl,--build-id=none']

qemu_args = ['-M', 'none', '-cpu', 'R5900', '-m', '16M',
             '-display', 'none',
             '-device', 'loader,file=@BINARY@',
             '-device', 'loader,addr=0xffffffff80100000,cpu-num=0']

# Pointers are 32-bit (o32, n32), sign-extend them for the 64-bit CPU
gdb_test = ['--gdb-args=-ex "set remoteaddresssize 64"',
            '--test', files('test-r5900.py')]

# o32: 32-bit pointers, 32-bit GPRs are enough
foreach t: [
  'test_r5900_div1.c',
  'test_r5900_divu1.c',
  'test_r5900_madd.c',
  'test_r5900_maddu.c',
  'test_r5900_mflohi1.c',
  'test_r5900_mtlohi1.c',
  'test_r5900_mult.c',
  'test_r5900_multu.c',
]
  tests += {
    t: {
      'cflags': cflags + ['-mabi=32'],
      'qemu_args': qemu_args,
      'gdb_test': gdb_test,
    }
  }
endforeach

if 'qemu-system-mips64el' in emulators
  tcg_tests += {
    'mips64el-softmmu': {
      'cc': 'mips64el-linux-gnuabi64-gcc',
      'cc_dockerfile': 'debian-all-test-cross',
      'folder': 'mips64el/system',
      'gdb_arch': 'mips64el',
      'qemu': emulators['qemu-system-mips64el'],
      'tests': tests,
    }
  }
endif
