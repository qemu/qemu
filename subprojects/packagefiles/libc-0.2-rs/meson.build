project('libc-0.2-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.2.162',
  license: 'MIT OR Apache-2.0',
  default_options: [])

_libc_rs = static_library(
  'libc',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2015', 'build.rust_std=2015'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'freebsd11',
    '--cfg', 'libc_priv_mod_use',
    '--cfg', 'libc_union',
    '--cfg', 'libc_const_size_of',
    '--cfg', 'libc_align',
    '--cfg', 'libc_int128',
    '--cfg', 'libc_core_cvoid',
    '--cfg', 'libc_packedN',
    '--cfg', 'libc_cfg_target_vendor',
    '--cfg', 'libc_non_exhaustive',
    '--cfg', 'libc_long_array',
    '--cfg', 'libc_ptr_addr_of',
    '--cfg', 'libc_underscore_const_names',
    '--cfg', 'libc_const_extern_fn',
  ],
  dependencies: [],
)

libc_dep = declare_dependency(
  link_with: _libc_rs,
)

meson.override_dependency('libc-0.2-rs', libc_dep)
