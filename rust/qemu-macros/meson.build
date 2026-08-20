_qemu_macros_rs = cargo_ws.package('qemu_macros').proc_macro()
cargo_ws.package('qemu_macros').override_dependency(declare_dependency(link_with: _qemu_macros_rs))

qemu_macros = declare_dependency(
  link_with: _qemu_macros_rs,
)

rust.test('rust-qemu-macros-tests', _qemu_macros_rs,
          suite: ['unit', 'rust'])
