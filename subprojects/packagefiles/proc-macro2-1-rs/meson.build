project('proc-macro2-1-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '1.0.95',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('unicode-ident-1-rs', required: true)

unicode_ident_dep = dependency('unicode-ident-1-rs', native: true)

_proc_macro2_rs = static_library(
  'proc_macro2',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="proc-macro"',
    '--cfg', 'no_literal_byte_character',
    '--cfg', 'no_literal_c_string',
    '--cfg', 'no_source_text',
    '--cfg', 'wrap_proc_macro',
  ],
  dependencies: [
    unicode_ident_dep,
  ],
  native: true,
)

proc_macro2_dep = declare_dependency(
  link_with: _proc_macro2_rs,
)

meson.override_dependency('proc-macro2-1-rs', proc_macro2_dep, native: true)
