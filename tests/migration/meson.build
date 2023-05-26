sysprof = dependency('sysprof-capture-4', required: false)
glib_static = dependency('glib-2.0', version: glib_req_ver, required: false,
                         method: 'pkg-config', static: true)

stress = executable(
  'stress',
  files('stress.c'),
  dependencies: [glib_static, sysprof],
  link_args: ['-static'],
  build_by_default: false,
)

custom_target(
  'initrd-stress.img',
  output: 'initrd-stress.img',
  input: stress,
  command: [find_program('initrd-stress.sh'), '@OUTPUT@', '@INPUT@']
)
