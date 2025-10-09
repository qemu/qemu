_hwcore_bindgen_args = []
c_enums = [
  'DeviceCategory',
  'GpioPolarity',
  'MachineInitPhase',
  'ResetType',
]
foreach enum : c_enums
  _hwcore_bindgen_args += ['--rustified-enum', enum]
endforeach

blocked_type = [
  'Chardev',
  'Error',
  'ObjectClass',
  'MemoryRegion',
  'VMStateDescription',
]
foreach type: blocked_type
  _hwcore_bindgen_args += ['--blocklist-type', type]
endforeach

c_bitfields = [
  'ClockEvent',
]
foreach enum : c_bitfields
  _hwcore_bindgen_args += ['--bitfield-enum', enum]
endforeach

# TODO: Remove this comment when the clang/libclang mismatch issue is solved.
#
# Rust bindings generation with `bindgen` might fail in some cases where the
# detected `libclang` does not match the expected `clang` version/target. In
# this case you must pass the path to `clang` and `libclang` to your build
# command invocation using the environment variables CLANG_PATH and
# LIBCLANG_PATH
_hwcore_bindings_inc_rs = rust.bindgen(
  input: 'wrapper.h',
  dependencies: common_ss.all_dependencies(),
  output: 'bindings.inc.rs',
  include_directories: bindings_incdir,
  bindgen_version: ['>=0.60.0'],
  args: bindgen_args_common + _hwcore_bindgen_args,
  c_args: bindgen_c_args,
)

_hwcore_rs = static_library(
  'hwcore',
  structured_sources(
    [
      'src/lib.rs',
      'src/bindings.rs',
      'src/irq.rs',
      'src/qdev.rs',
      'src/sysbus.rs',
    ],
    {'.': _hwcore_bindings_inc_rs}
  ),
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  link_with: [_bql_rs, _chardev_rs, _migration_rs, _qom_rs, _system_rs, _util_rs],
  dependencies: [glib_sys_rs, qemu_macros, common_rs],
)

hwcore_rs = declare_dependency(link_with: [_hwcore_rs],
  dependencies: [qom_rs, hwcore])

test('rust-hwcore-rs-integration',
    executable(
        'rust-hwcore-rs-integration',
        files('tests/tests.rs'),
        override_options: ['rust_std=2021', 'build.rust_std=2021'],
        rust_args: ['--test'],
        install: false,
        dependencies: [common_rs, hwcore_rs, bql_rs, migration_rs, util_rs]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
