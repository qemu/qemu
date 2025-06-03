subproject('bilge-0.2-rs', required: true)
subproject('bilge-impl-0.2-rs', required: true)
subproject('libc-0.2-rs', required: true)

bilge_rs = dependency('bilge-0.2-rs')
bilge_impl_rs = dependency('bilge-impl-0.2-rs')
libc_rs = dependency('libc-0.2-rs')

subproject('proc-macro2-1-rs', required: true)
subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)

quote_rs_native = dependency('quote-1-rs', native: true)
syn_rs_native = dependency('syn-2-rs', native: true)
proc_macro2_rs_native = dependency('proc-macro2-1-rs', native: true)

qemuutil_rs = qemuutil.partial_dependency(link_args: true, links: true)

subdir('qemu-api-macros')
subdir('bits')
subdir('qemu-api')

subdir('hw')

cargo = find_program('cargo', required: false)

if cargo.found()
  run_target('rustfmt',
    command: [config_host['MESON'], 'devenv',
              '--workdir', '@CURRENT_SOURCE_DIR@',
              cargo, 'fmt'],
    depends: bindings_rs)
endif
