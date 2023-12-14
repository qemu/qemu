# TARGET_BIG_ENDIAN is defined to 'n' for little-endian; which means it
# is always true as far as source_set.apply() is concerned.  Always build
# both header files and include the right one via #if.

vdso_be_inc = gen_vdso.process('vdso-be.so',
                               extra_args: ['-r', '__kernel_rt_sigreturn'])

vdso_le_inc = gen_vdso.process('vdso-le.so',
                               extra_args: ['-r', '__kernel_rt_sigreturn'])

linux_user_ss.add(when: 'TARGET_AARCH64', if_true: [vdso_be_inc, vdso_le_inc])
