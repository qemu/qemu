project('syn-2-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '2.0.66',
  license: 'MIT OR Apache-2.0',
  default_options: [])

subproject('proc-macro2-1-rs', required: true)
subproject('quote-1-rs', required: true)
subproject('unicode-ident-1-rs', required: true)

proc_macro2_dep = dependency('proc-macro2-1-rs', native: true)
quote_dep = dependency('quote-1-rs', native: true)
unicode_ident_dep = dependency('unicode-ident-1-rs', native: true)

_syn_rs = static_library(
  'syn',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="full"',
    '--cfg', 'feature="derive"',
    '--cfg', 'feature="parsing"',
    '--cfg', 'feature="printing"',
    '--cfg', 'feature="clone-impls"',
    '--cfg', 'feature="proc-macro"',
    '--cfg', 'feature="extra-traits"',
  ],
  dependencies: [
    quote_dep,
    proc_macro2_dep,
    unicode_ident_dep,
  ],
  native: true,
)

syn_dep = declare_dependency(
  link_with: _syn_rs,
)

meson.override_dependency('syn-2-rs', syn_dep, native: true)
