project('glib-sys-0.21-rs', 'rust',
  meson_version: '>=1.5.0',
  version: '0.21.2',
  license: 'MIT',
  default_options: [])

subproject('libc-0.2-rs', required: true)
libc_rs = dependency('libc-0.2-rs')

_glib_sys_rs = static_library(
  'glib_sys',
  files('src/lib.rs'),
  gnu_symbol_visibility: 'hidden',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cap-lints', 'allow',
    '--cfg', 'feature="v2_66"',
    '--cfg', 'feature="v2_64"',
    '--cfg', 'feature="v2_62"',
    '--cfg', 'feature="v2_60"',
    '--cfg', 'feature="v2_58"',
  ],
  # should also link with glib; don't bother doing it here since all
  # QEMU targets have it
  dependencies: [libc_rs],
)

glib_sys_dep = declare_dependency(
  link_with: _glib_sys_rs,
)

meson.override_dependency('glib-sys-0.21-rs', glib_sys_dep)
