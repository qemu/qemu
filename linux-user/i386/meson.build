syscall_nr_generators += {
  'i386': generator(sh,
                    arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                    output: '@BASENAME@_nr.h')
}

vdso_inc = gen_vdso.process('vdso.so', extra_args: [
                                '-s', '__kernel_sigreturn',
                                '-r', '__kernel_rt_sigreturn'
                            ])

linux_user_ss.add(when: 'TARGET_I386', if_true: vdso_inc)
