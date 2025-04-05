_qemu_api_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

# _qemu_api_cfg += ['--cfg', 'feature="allocator"']
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
      'src/errno.rs',
      'src/irq.rs',
      'src/memory.rs',
      'src/module.rs',
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
  dependencies: [libc_rs, qemu_api_macros, qemuutil_rs,
                 qom, hwcore, chardev, migration],
)

rust.test('rust-qemu-api-tests', _qemu_api_rs,
          suite: ['unit', 'rust'])

qemu_api = declare_dependency(link_with: [_qemu_api_rs],
  dependencies: [qemu_api_macros, qom, hwcore, chardev, migration])

# Doctests are essentially integration tests, so they need the same dependencies.
# Note that running them requires the object files for C code, so place them
# in a separate suite that is run by the "build" CI jobs rather than "check".
rust.doctest('rust-qemu-api-doctests',
     _qemu_api_rs,
     protocol: 'rust',
     dependencies: qemu_api,
     suite: ['doc', 'rust'])

test('rust-qemu-api-integration',
    executable(
        'rust-qemu-api-integration',
        files('tests/tests.rs', 'tests/vmstate_tests.rs'),
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'],
        install: false,
        dependencies: [qemu_api]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
