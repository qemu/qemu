syscall_nr_generators += {
  'x86_64': generator(sh,
                      arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                      output: '@BASENAME@_nr.h')
}

vdso_inc = gen_vdso.process('vdso.so')

linux_user_ss.add(when: 'TARGET_X86_64', if_true: vdso_inc)
