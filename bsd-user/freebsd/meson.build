bsd_syscall_nr = custom_target('bsd-syscall-h',
    output: '@BASENAME@_nr.h',
    input: ['/usr/include/sys/syscall.h'],
    command: [sh, meson.current_source_dir() / 'scripts/syscallhdr.sh', '@INPUT@', '@OUTPUT@', 'FREEBSD'])

bsd_user_ss.add(files(
  'os-extattr.c',
  'os-proc.c',
  'os-socket.c',
  'os-stat.c',
  'os-sys.c',
  'os-syscall.c',
  'os-thread.c',
  'os-time.c'),
  bsd_syscall_nr
)
