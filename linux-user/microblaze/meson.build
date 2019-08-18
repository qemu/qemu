syscall_nr_generators += {
  'microblaze': generator(sh,
                          arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                          output: '@BASENAME@_nr.h')
}
