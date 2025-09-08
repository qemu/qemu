_qemu_api_cfg = run_command(rustc_args,
  '--config-headers', config_host_h, '--features', files('Cargo.toml'),
  capture: true, check: true).stdout().strip().splitlines()

c_enums = [
  'DeviceCategory',
  'GpioPolarity',
  'MachineInitPhase',
  'MemoryDeviceInfoKind',
  'QEMUChrEvent',
  'ResetType',
  'device_endian',
]
_qemu_api_bindgen_args = []
foreach enum : c_enums
  _qemu_api_bindgen_args += ['--rustified-enum', enum]
endforeach
c_bitfields = [
  'ClockEvent',
]
foreach enum : c_bitfields
  _qemu_api_bindgen_args += ['--bitfield-enum', enum]
endforeach

_qemu_api_bindgen_args += ['--blocklist-type', 'VMStateDescription']

_qemu_api_bindgen_args += ['--blocklist-type', 'Error']
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
      'src/bindings.rs',
      'src/chardev.rs',
      'src/irq.rs',
      'src/memory.rs',
      'src/prelude.rs',
      'src/qdev.rs',
      'src/qom.rs',
      'src/sysbus.rs',
    ],
    {'.' : _qemu_api_bindings_inc_rs},
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  rust_args: _qemu_api_cfg,
  dependencies: [anyhow_rs, bql_rs, common_rs, foreign_rs, libc_rs, migration_rs, qemu_api_macros,
                 util_rs, qom, hwcore, chardev],
)

qemu_api_rs = declare_dependency(link_with: [_qemu_api_rs],
  dependencies: [qemu_api_macros, qom, hwcore, chardev, migration])

test('rust-qemu-api-integration',
    executable(
        'rust-qemu-api-integration',
        files('tests/tests.rs', 'tests/vmstate_tests.rs'),
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'],
        install: false,
        dependencies: [bql_rs, common_rs, util_rs, migration_rs, qemu_api_rs]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
