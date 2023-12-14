vdso_32_inc = gen_vdso.process('vdso-32.so',
                               extra_args: ['-r', '__vdso_rt_sigreturn'])
vdso_64_inc = gen_vdso.process('vdso-64.so',
                               extra_args: ['-r', '__vdso_rt_sigreturn'])

linux_user_ss.add(when: 'TARGET_RISCV32', if_true: vdso_32_inc)
linux_user_ss.add(when: 'TARGET_RISCV64', if_true: vdso_64_inc)
