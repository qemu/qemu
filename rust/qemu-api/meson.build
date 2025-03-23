_qemu_api_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

libc_dep = dependency('libc-0.2-rs')

# _qemu_api_cfg += ['--cfg', 'feature="allocator"']
if rustc.version().version_compare('>=1.77.0')
  _qemu_api_cfg += ['--cfg', 'has_offset_of']
endif
if get_option('debug_mutex')
  _qemu_api_cfg += ['--cfg', 'feature="debug_cell"']
endif

_qemu_api_rs = static_library(
  'qemu_api',
  structured_sources(
    [
      'src/lib.rs',
      'src/assertions.rs',
      'src/bindings.rs',
      'src/bitops.rs',
      'src/callbacks.rs',
      'src/cell.rs',
      'src/chardev.rs',
      'src/c_str.rs',
      'src/errno.rs',
      'src/irq.rs',
      'src/memory.rs',
      'src/module.rs',
      'src/offset_of.rs',
      'src/prelude.rs',
      'src/qdev.rs',
      'src/qom.rs',
      'src/sysbus.rs',
      'src/timer.rs',
      'src/vmstate.rs',
      'src/zeroable.rs',
    ],
    {'.' : bindings_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg,
  dependencies: [libc_dep, qemu_api_macros],
)

rust.test('rust-qemu-api-tests', _qemu_api_rs,
          suite: ['unit', 'rust'])

qemu_api = declare_dependency(link_with: _qemu_api_rs)

# Rust executables do not support objects, so add an intermediate step.
rust_qemu_api_objs = static_library(
    'rust_qemu_api_objs',
    objects: [libqom.extract_all_objects(recursive: false),
              libhwcore.extract_all_objects(recursive: false),
              libchardev.extract_all_objects(recursive: false),
              libcrypto.extract_all_objects(recursive: false),
              libauthz.extract_all_objects(recursive: false),
              libio.extract_all_objects(recursive: false),
              libmigration.extract_all_objects(recursive: false)])
rust_qemu_api_deps = declare_dependency(
    dependencies: [
      qom_ss.dependencies(),
      chardev_ss.dependencies(),
      crypto_ss.dependencies(),
      authz_ss.dependencies(),
      io_ss.dependencies()],
    link_whole: [rust_qemu_api_objs, libqemuutil])

test('rust-qemu-api-integration',
    executable(
        'rust-qemu-api-integration',
        files('tests/tests.rs', 'tests/vmstate_tests.rs'),
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'],
        install: false,
        dependencies: [qemu_api, qemu_api_macros, rust_qemu_api_deps]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
