vdso_inc = gen_vdso.process('vdso.so',
                            extra_args: ['-r', '__vdso_rt_sigreturn'])

linux_user_ss.add(when: 'TARGET_LOONGARCH64', if_true: vdso_inc)
