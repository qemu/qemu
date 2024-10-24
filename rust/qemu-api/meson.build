_qemu_api_cfg = ['--cfg', 'MESON']
# _qemu_api_cfg += ['--cfg', 'feature="allocator"']
if rustc.version().version_compare('>=1.77.0')
  _qemu_api_cfg += ['--cfg', 'has_offset_of']
endif

_qemu_api_rs = static_library(
  'qemu_api',
  structured_sources(
    [
      'src/lib.rs',
      'src/c_str.rs',
      'src/definitions.rs',
      'src/device_class.rs',
      'src/offset_of.rs',
      'src/vmstate.rs',
      'src/zeroable.rs',
    ],
    {'.' : bindings_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg,
)

rust.test('rust-qemu-api-tests', _qemu_api_rs,
          suite: ['unit', 'rust'])

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
