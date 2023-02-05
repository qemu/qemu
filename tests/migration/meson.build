sysprof = dependency('sysprof-capture-4', required: false)

stress = executable(
  'stress',
  files('stress.c'),
  dependencies: [glib, sysprof],
  link_args: ['-static'],
  build_by_default: false,
)

custom_target(
  'initrd-stress.img',
  output: 'initrd-stress.img',
  input: stress,
  command: [find_program('initrd-stress.sh'), '@OUTPUT@', '@INPUT@']
)
