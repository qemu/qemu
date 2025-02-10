subproject('proc-macro2-1-rs', required: true)
subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)

quote_dep = dependency('quote-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)
proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)

_qemu_api_macros_rs = rust.proc_macro(
  'qemu_api_macros',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: [
    '--cfg', 'use_fallback',
    '--cfg', 'feature="syn-error"',
    '--cfg', 'feature="proc-macro"',
  ],
  dependencies: [
    proc_macro2_dep,
    quote_dep,
    syn_dep,
  ],
)

qemu_api_macros = declare_dependency(
  link_with: _qemu_api_macros_rs,
)
