subdir('nwfpe')

syscall_nr_generators += {
  'arm': generator(sh,
                   arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@' ],
                   output: '@BASENAME@_nr.h')
}

# TARGET_BIG_ENDIAN is defined to 'n' for little-endian; which means it
# is always true as far as source_set.apply() is concerned.  Always build
# both header files and include the right one via #if.

vdso_be_inc = gen_vdso.process('vdso-be.so',
                               extra_args: ['-s', 'sigreturn_codes'])

vdso_le_inc = gen_vdso.process('vdso-le.so',
                               extra_args: ['-s', 'sigreturn_codes'])

linux_user_ss.add(when: 'TARGET_ARM', if_true: [vdso_be_inc, vdso_le_inc])
