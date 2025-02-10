project('bilge-impl-0.2-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.2.0',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('itertools-0.11-rs', required: true)
subproject('proc-macro-error-attr-1-rs', required: true)
subproject('proc-macro-error-1-rs', required: true)
subproject('quote-1-rs', required: true)
subproject('syn-2-rs', required: true)
subproject('proc-macro2-1-rs', required: true)

itertools_dep = dependency('itertools-0.11-rs', native: true)
proc_macro_error_attr_dep = dependency('proc-macro-error-attr-1-rs', native: true)
proc_macro_error_dep = dependency('proc-macro-error-1-rs', native: true)
quote_dep = dependency('quote-1-rs', native: true)
syn_dep = dependency('syn-2-rs', native: true)
proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)

rust = import('rust')

_bilge_impl_rs = rust.proc_macro(
  'bilge_impl',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'use_fallback',
    '--cfg', 'feature="syn-error"',
    '--cfg', 'feature="proc-macro"',
  ],
  dependencies: [
    itertools_dep,
    proc_macro_error_attr_dep,
    proc_macro_error_dep,
    quote_dep,
    syn_dep,
    proc_macro2_dep,
  ],
)

bilge_impl_dep = declare_dependency(
  link_with: _bilge_impl_rs,
)

meson.override_dependency('bilge-impl-0.2-rs', bilge_impl_dep)
