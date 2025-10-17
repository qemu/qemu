if not have_system
  subdir_done()
else
  message('Rust enabled but it is only used by system emulators.')
endif

cargo_ws = import('rust').workspace()
anyhow_rs = cargo_ws.subproject('anyhow').dependency()
bilge_rs = cargo_ws.subproject('bilge').dependency()
bilge_impl_rs = cargo_ws.subproject('bilge-impl').dependency()
foreign_rs = cargo_ws.subproject('foreign').dependency()
glib_sys_rs = cargo_ws.subproject('glib-sys').dependency()
libc_rs = cargo_ws.subproject('libc').dependency()
probe_rs = cargo_ws.subproject('probe').dependency()
quote_rs_native = cargo_ws.subproject('quote').dependency()
syn_rs_native = cargo_ws.subproject('syn').dependency()
proc_macro2_rs_native = cargo_ws.subproject('proc-macro2').dependency()
attrs_rs_native = cargo_ws.subproject('attrs').dependency()

genrs = []

subdir('qemu-macros')

subdir('common')
subdir('bindings')
subdir('bits')

subdir('util')
subdir('bql')
subdir('migration')
subdir('qom')
subdir('chardev')
subdir('hw/core')
subdir('system')
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
