_qemu_macros_rs = rust.proc_macro(
  'qemu_macros',
  files('src/lib.rs'),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_args: [
    '--cfg', 'use_fallback',
    '--cfg', 'feature="syn-error"',
    '--cfg', 'feature="proc-macro"',
  ],
  dependencies: [
    attrs_rs_native,
    proc_macro2_rs_native,
    quote_rs_native,
    syn_rs_native,
  ],
)

qemu_macros = declare_dependency(
  link_with: _qemu_macros_rs,
)

rust.test('rust-qemu-macros-tests', _qemu_macros_rs,
          suite: ['unit', 'rust'])
