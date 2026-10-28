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
