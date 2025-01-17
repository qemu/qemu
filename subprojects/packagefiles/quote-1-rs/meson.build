project('quote-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.12.0',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('proc-macro2-1-rs', required: true)

proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)

_quote_rs = static_library(
  'quote',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="proc-macro"',
  ],
  dependencies: [
    proc_macro2_dep,
  ],
  native: true,
)

quote_dep = declare_dependency(
  link_with: _quote_rs,
)

meson.override_dependency('quote-1-rs', quote_dep, native: true)
