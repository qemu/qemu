_qemu_api_rs = static_library(
  'qemu_api',
  structured_sources(
    [
      'src/lib.rs',
      'src/definitions.rs',
      'src/device_class.rs',
      'src/vmstate.rs',
      'src/zeroable.rs',
    ],
    {'.' : bindings_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: [
    '--cfg', 'MESON',
    # '--cfg', 'feature="allocator"',
  ],
)

qemu_api = declare_dependency(
  link_with: _qemu_api_rs,
  dependencies: qemu_api_macros,
)

# Rust executables do not support objects, so add an intermediate step.
rust_qemu_api_objs = static_library(
    'rust_qemu_api_objs',
    objects: [libqom.extract_all_objects(recursive: false),
              libhwcore.extract_all_objects(recursive: false)])

test('rust-qemu-api-integration',
    executable(
        'rust-qemu-api-integration',
        'tests/tests.rs',
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'],
        install: false,
        dependencies: [qemu_api, qemu_api_macros],
        link_whole: [rust_qemu_api_objs, libqemuutil]),
    args: [
        '--test',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
