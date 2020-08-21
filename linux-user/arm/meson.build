linux_user_ss.add(when: 'TARGET_AARCH64', if_true: files('semihost.c'))
linux_user_ss.add(when: 'TARGET_ARM', if_true: files('semihost.c'))

subdir('nwfpe')

syscall_nr_generators += {
  'arm': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}
