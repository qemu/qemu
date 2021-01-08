subdir('nwfpe')

syscall_nr_generators += {
  'arm': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}
