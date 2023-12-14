syscall_nr_generators += {
  'ppc': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}

vdso_32_inc = gen_vdso.process('vdso-32.so', extra_args: [
                                   '-s', '__kernel_sigtramp32',
                                   '-r', '__kernel_sigtramp_rt32'
                               ])
linux_user_ss.add(when: 'TARGET_PPC', if_true: vdso_32_inc)

vdso_64_inc = gen_vdso.process('vdso-64.so',
                               extra_args: ['-r', '__kernel_sigtramp_rt64'])
vdso_64le_inc = gen_vdso.process('vdso-64le.so',
                                 extra_args: ['-r', '__kernel_sigtramp_rt64'])
linux_user_ss.add(when: 'TARGET_PPC64', if_true: [vdso_64_inc, vdso_64le_inc])
