project('proc-macro-error-attr-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.12.0',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('proc-macro2-1-rs', required: true)
subproject('quote-1-rs', required: true)

proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)
quote_dep = dependency('quote-1-rs', native: true)

rust = import('rust')
_proc_macro_error_attr_rs = rust.proc_macro(
  'proc_macro_error_attr',
  files('src/lib.rs'),
  override_options: ['rust_std=2018', 'build.rust_std=2018'],
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'use_fallback',
    '--cfg', 'feature="syn-error"',
    '--cfg', 'feature="proc-macro"'
  ],
  dependencies: [
    proc_macro2_dep,
    quote_dep,
  ],
)

proc_macro_error_attr_dep = declare_dependency(
  link_with: _proc_macro_error_attr_rs,
)

meson.override_dependency('proc-macro-error-attr-1-rs', proc_macro_error_attr_dep, native: true)
