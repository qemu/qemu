_qemu_api_rs = static_library(
  'qemu_api',
  structured_sources(
    [
      'src/lib.rs',
      'src/definitions.rs',
      'src/device_class.rs',
    ],
    {'.' : bindings_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: rustc_args + [
    '--cfg', 'MESON',
    # '--cfg', 'feature="allocator"',
  ],
  dependencies: [
    qemu_api_macros,
  ],
)

qemu_api = declare_dependency(
  link_with: _qemu_api_rs,
)
