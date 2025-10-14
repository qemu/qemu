subproject('anyhow-1-rs', required: true)
subproject('bilge-0.2-rs', required: true)
subproject('bilge-impl-0.2-rs', required: true)
subproject('foreign-0.3-rs', required: true)
subproject('glib-sys-0.21-rs', required: true)
subproject('libc-0.2-rs', required: true)

anyhow_rs = dependency('anyhow-1-rs')
bilge_rs = dependency('bilge-0.2-rs')
bilge_impl_rs = dependency('bilge-impl-0.2-rs')
foreign_rs = dependency('foreign-0.3-rs')
glib_sys_rs = dependency('glib-sys-0.21-rs')
libc_rs = dependency('libc-0.2-rs')

subproject('proc-macro2-1-rs', required: true)
subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)
subproject('attrs-0.2-rs', required: true)

quote_rs_native = dependency('quote-1-rs', native: true)
syn_rs_native = dependency('syn-2-rs', native: true)
proc_macro2_rs_native = dependency('proc-macro2-1-rs', native: true)
attrs_rs_native = dependency('attrs-0.2-rs', native: true)

genrs = []

subdir('qemu-macros')

subdir('common')
subdir('bits')
subdir('util')
subdir('bql')
subdir('migration')
subdir('qom')
subdir('system')
subdir('chardev')
subdir('hw/core')
subdir('tests')
subdir('trace')
subdir('hw')

cargo = find_program('cargo', required: false)

if cargo.found()
  run_target('rustfmt',
    command: [config_host['MESON'], 'devenv',
              '--workdir', '@CURRENT_SOURCE_DIR@',
              cargo, 'fmt'],
    depends: genrs)
endif
