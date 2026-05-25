syscall_nr_generators += {
  'sh4': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}

vdso_le_inc = gen_vdso.process('vdso-le.so',
                               extra_args: ['-s', '__kernel_sigreturn',
                                            '-r', '__kernel_rt_sigreturn',
                                            '-p', 'vdso_le'])

vdso_be_inc = gen_vdso.process('vdso-be.so',
                               extra_args: ['-s', '__kernel_sigreturn',
                                            '-r', '__kernel_rt_sigreturn',
                                            '-p', 'vdso_be'])

linux_user_ss.add(when: 'TARGET_SH4', if_true: [vdso_le_inc, vdso_be_inc])
