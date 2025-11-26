test('rust-integration',
    executable(
        'rust-integration',
        files('tests/vmstate_tests.rs'),
        rust_args: ['--test'],
        install: false,
        dependencies: [bql_rs, common_rs, util_rs, migration_rs, qom_rs]),
    args: [
        '--test', '--test-threads', '1',
        '--format', 'pretty',
    ],
    protocol: 'rust',
    suite: ['unit', 'rust'])
