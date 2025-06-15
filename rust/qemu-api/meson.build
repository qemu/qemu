_qemu_api_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

# _qemu_api_cfg += ['--cfg', 'feature="allocator"']
if get_option('debug_mutex')
  _qemu_api_cfg += ['--cfg', 'feature="debug_cell"']
endif

c_enums = [
  'DeviceCategory',
  'GpioPolarity',
  'MachineInitPhase',
  'MemoryDeviceInfoKind',
  'MigrationPolicy',
  'MigrationPriority',
  'QEMUChrEvent',
  'QEMUClockType',
  'ResetType',
  'device_endian',
  'module_init_type',
]
_qemu_api_bindgen_args = []
foreach enum : c_enums
  _qemu_api_bindgen_args += ['--rustified-enum', enum]
endforeach
c_bitfields = [
  'ClockEvent',
  'VMStateFlags',
]
foreach enum : c_bitfields
  _qemu_api_bindgen_args += ['--bitfield-enum', enum]
endforeach

# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_qemu_api_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common + _qemu_api_bindgen_args,
  )

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
      'src/error.rs',
      'src/irq.rs',
      'src/log.rs',
      'src/memory.rs',
      'src/module.rs',
      'src/prelude.rs',
      'src/qdev.rs',
      'src/qom.rs',
      'src/sysbus.rs',
      'src/timer.rs',
      'src/uninit.rs',
      'src/vmstate.rs',
      'src/zeroable.rs',
    ],
    {'.' : _qemu_api_bindings_inc_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg,
  dependencies: [anyhow_rs, foreign_rs, libc_rs, qemu_api_macros, qemuutil_rs,
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
