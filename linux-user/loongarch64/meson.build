vdso_inc = gen_vdso.process('vdso.so',
                            extra_args: ['-r', '__vdso_rt_sigreturn'])

linux_user_ss.add(when: 'TARGET_LOONGARCH64', if_true: vdso_inc)


syscall_nr_generators += {
  'loongarch64': generator(sh,
                      arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                      output: '@BASENAME@_nr.h')
}
