_hwcore_rs = cargo_ws.package('hwcore').library()
cargo_ws.package('hwcore').override_dependency(declare_dependency(link_with: _hwcore_rs))

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
