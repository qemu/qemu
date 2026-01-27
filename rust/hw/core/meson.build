_hwcore_rs = static_library(
  'hwcore',
  'src/lib.rs',
  override_options: ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
  link_with: [_bql_rs, _chardev_rs, _migration_rs, _qom_rs, _util_rs],
  dependencies: [glib_sys_rs, qemu_macros, common_rs, hwcore_sys_rs],
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
        dependencies: [chardev_rs, common_rs, hwcore_rs, bql_rs, migration_rs, util_rs]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
