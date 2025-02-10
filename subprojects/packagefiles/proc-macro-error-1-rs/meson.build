project('proc-macro-error-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.4',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('proc-macro-error-attr-1-rs', required: true)
subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)
subproject('proc-macro2-1-rs', required: true)

proc_macro_error_attr_dep = dependency('proc-macro-error-attr-1-rs', native: true)
proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)
quote_dep = dependency('quote-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)

_proc_macro_error_rs = static_library(
  'proc_macro_error',
  files('src/lib.rs'),
  override_options: ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'use_fallback',
    '--cfg', 'feature="syn-error"',
    '--cfg', 'feature="proc-macro"',
    '-A', 'non_fmt_panics'
  ],
  dependencies: [
    proc_macro_error_attr_dep,
    proc_macro2_dep,
    quote_dep,
    syn_dep,
  ],
  native: true,
)

proc_macro_error_dep = declare_dependency(
  link_with: _proc_macro_error_rs,
)

meson.override_dependency('proc-macro-error-1-rs', proc_macro_error_dep, native: true)
