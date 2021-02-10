py3 = import('python').find_installation()

qht_bench = executable('qht-bench',
                       sources: 'qht-bench.c',
                       dependencies: [qemuutil])

executable('atomic_add-bench',
           sources: files('atomic_add-bench.c'),
           dependencies: [qemuutil],
           build_by_default: false)

executable('atomic64-bench',
           sources: files('atomic64-bench.c'),
           dependencies: [qemuutil],
           build_by_default: false)

test_qapi_outputs = [
  'qapi-builtin-types.c',
  'qapi-builtin-types.h',
  'qapi-builtin-visit.c',
  'qapi-builtin-visit.h',
  'test-qapi-commands-sub-sub-module.c',
  'test-qapi-commands-sub-sub-module.h',
  'test-qapi-commands.c',
  'test-qapi-commands.h',
  'test-qapi-emit-events.c',
  'test-qapi-emit-events.h',
  'test-qapi-events-sub-sub-module.c',
  'test-qapi-events-sub-sub-module.h',
  'test-qapi-events.c',
  'test-qapi-events.h',
  'test-qapi-init-commands.c',
  'test-qapi-init-commands.h',
  'test-qapi-introspect.c',
  'test-qapi-introspect.h',
  'test-qapi-types-sub-sub-module.c',
  'test-qapi-types-sub-sub-module.h',
  'test-qapi-types.c',
  'test-qapi-types.h',
  'test-qapi-visit-sub-sub-module.c',
  'test-qapi-visit-sub-sub-module.h',
  'test-qapi-visit.c',
  'test-qapi-visit.h',
]

test_qapi_files = custom_target('Test QAPI files',
                                output: test_qapi_outputs,
                                input: files('qapi-schema/qapi-schema-test.json',
                                             'qapi-schema/include/sub-module.json',
                                             'qapi-schema/sub-sub-module.json'),
                                command: [ qapi_gen, '-o', meson.current_build_dir(),
                                           '-b', '-p', 'test-', '@INPUT0@' ],
                                depend_files: qapi_gen_depends)

# meson doesn't like generated output in other directories
# perhaps change qapi_gen to replace / with _, like Meson itself does?
subdir('include')

test_qapi_sources = []
test_qapi_headers = []
i = 0
foreach o: test_qapi_files.to_list() + test_qapi_files_extra.to_list()
  if o.full_path().endswith('.h')
    test_qapi_headers += o
  endif
  test_qapi_sources += o
endforeach

libtestqapi = static_library('testqapi', sources: [genh, test_qapi_sources])
testqapi = declare_dependency(link_with: libtestqapi, sources: [genh, test_qapi_headers])

testblock = declare_dependency(dependencies: [block], sources: 'iothread.c')

tests = {
  'check-block-qdict': [],
  'check-qdict': [],
  'check-qnum': [],
  'check-qstring': [],
  'check-qlist': [],
  'check-qnull': [],
  'check-qobject': [],
  'check-qjson': [],
  'check-qlit': [],
  'test-qobject-output-visitor': [testqapi],
  'test-clone-visitor': [testqapi],
  'test-qobject-input-visitor': [testqapi],
  'test-string-input-visitor': [testqapi],
  'test-string-output-visitor': [testqapi],
  'test-opts-visitor': [testqapi],
  'test-visitor-serialization': [testqapi],
  'test-bitmap': [],
  # all code tested by test-x86-cpuid is inside topology.h
  'test-x86-cpuid': [],
  'test-cutils': [],
  'test-shift128': [],
  'test-mul64': [],
  # all code tested by test-int128 is inside int128.h
  'test-int128': [],
  'rcutorture': [],
  'test-rcu-list': [],
  'test-rcu-simpleq': [],
  'test-rcu-tailq': [],
  'test-rcu-slist': [],
  'test-qdist': [],
  'test-qht': [],
  'test-bitops': [],
  'test-bitcnt': [],
  'test-qgraph': ['qtest/libqos/qgraph.c'],
  'check-qom-interface': [qom],
  'check-qom-proplist': [qom],
  'test-qemu-opts': [],
  'test-keyval': [testqapi],
  'test-logging': [],
  'test-uuid': [],
  'ptimer-test': ['ptimer-test-stubs.c', meson.source_root() / 'hw/core/ptimer.c'],
  'test-qapi-util': [],
}

if have_system or have_tools
  tests += {
    'test-qmp-event': [testqapi],
  }
endif

test_deps = {
  'test-qht-par': qht_bench,
}

benchs = {}

if have_block
  tests += {
    'test-coroutine': [testblock],
    'test-aio': [testblock],
    'test-aio-multithread': [testblock],
    'test-throttle': [testblock],
    'test-thread-pool': [testblock],
    'test-hbitmap': [testblock],
    'test-bdrv-drain': [testblock],
    'test-bdrv-graph-mod': [testblock],
    'test-blockjob': [testblock],
    'test-blockjob-txn': [testblock],
    'test-block-backend': [testblock],
    'test-block-iothread': [testblock],
    'test-write-threshold': [testblock],
    'test-crypto-hash': [crypto],
    'test-crypto-hmac': [crypto],
    'test-crypto-cipher': [crypto],
    'test-crypto-secret': [crypto, keyutils],
    'test-authz-simple': [authz],
    'test-authz-list': [authz],
    'test-authz-listfile': [authz],
    'test-io-task': [testblock],
    'test-io-channel-socket': ['socket-helpers.c', 'io-channel-helpers.c', io],
    'test-io-channel-file': ['io-channel-helpers.c', io],
    'test-io-channel-command': ['io-channel-helpers.c', io],
    'test-io-channel-buffer': ['io-channel-helpers.c', io],
    'test-crypto-ivgen': [io],
    'test-crypto-afsplit': [io],
    'test-crypto-block': [io],
  }
  if 'CONFIG_GNUTLS' in config_host and \
     'CONFIG_TASN1' in config_host and \
     'CONFIG_POSIX' in config_host
    tests += {
      'test-crypto-tlscredsx509': ['crypto-tls-x509-helpers.c', 'pkix_asn1_tab.c',
                                   tasn1, crypto, gnutls],
      'test-crypto-tlssession': ['crypto-tls-x509-helpers.c', 'pkix_asn1_tab.c', 'crypto-tls-psk-helpers.c',
                                 tasn1, crypto, gnutls],
      'test-io-channel-tls': ['io-channel-helpers.c', 'crypto-tls-x509-helpers.c', 'pkix_asn1_tab.c',
                              tasn1, io, crypto, gnutls]}
  endif
  if 'CONFIG_AUTH_PAM' in config_host
    tests += {'test-authz-pam': [authz]}
  endif
  if 'CONFIG_QEMU_PRIVATE_XTS' in config_host
    tests += {'test-crypto-xts': [crypto, io]}
  endif
  if 'CONFIG_POSIX' in config_host
    tests += {'test-image-locking': [testblock]}
  endif
  if 'CONFIG_REPLICATION' in config_host
    tests += {'test-replication': [testblock]}
  endif
  if 'CONFIG_NETTLE' in config_host or 'CONFIG_GCRYPT' in config_host
    tests += {'test-crypto-pbkdf': [io]}
  endif
  if 'CONFIG_EPOLL_CREATE1' in config_host
    tests += {'test-fdmon-epoll': [testblock]}
  endif
  benchs += {
     'benchmark-crypto-hash': [crypto],
     'benchmark-crypto-hmac': [crypto],
     'benchmark-crypto-cipher': [crypto],
  }
endif

if have_system
  tests += {
    'test-iov': [],
    'test-qmp-cmds': [testqapi],
    'test-xbzrle': [migration],
    'test-timed-average': [],
    'test-util-sockets': ['socket-helpers.c'],
    'test-base64': [],
    'test-bufferiszero': [],
    'test-vmstate': [migration, io]
  }
  if 'CONFIG_INOTIFY1' in config_host
    tests += {'test-util-filemonitor': []}
  endif

  # Some tests: test-char, test-qdev-global-props, and test-qga,
  # are not runnable under TSan due to a known issue.
  # https://github.com/google/sanitizers/issues/1116
  if 'CONFIG_TSAN' not in config_host
    if 'CONFIG_POSIX' in config_host
        tests += {
          'test-char': ['socket-helpers.c', qom, io, chardev]
        }
    endif

    tests += {
      'test-qdev-global-props': [qom, hwcore, testqapi]
    }
  endif
endif

if 'CONFIG_TSAN' not in config_host and \
   'CONFIG_GUEST_AGENT' in config_host and \
   'CONFIG_LINUX' in config_host
  tests += {'test-qga': ['qtest/libqtest.c']}
  test_deps += {'test-qga': qga}
endif

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

foreach test_name, extra: tests
  src = [test_name + '.c']
  deps = [qemuutil]
  if extra.length() > 0
    # use a sourceset to quickly separate sources and deps
    test_ss = ss.source_set()
    test_ss.add(extra)
    src += test_ss.all_sources()
    deps += test_ss.all_dependencies()
  endif
  exe = executable(test_name, src, genh, dependencies: deps)

  test(test_name, exe,
       depends: test_deps.get(test_name, []),
       env: test_env,
       args: ['--tap', '-k'],
       protocol: 'tap',
       suite: ['unit'])
endforeach

foreach bench_name, deps: benchs
  exe = executable(bench_name, bench_name + '.c',
                   dependencies: [qemuutil] + deps)
  benchmark(bench_name, exe,
            args: ['--tap', '-k'],
            protocol: 'tap',
            suite: ['speed'])
endforeach

if have_tools and 'CONFIG_VHOST_USER' in config_host and 'CONFIG_LINUX' in config_host
  executable('vhost-user-bridge',
             sources: files('vhost-user-bridge.c'),
             dependencies: [qemuutil, vhost_user])
endif

if have_system and 'CONFIG_POSIX' in config_host
  subdir('qemu-iotests')
endif

test('decodetree', sh,
     args: [ files('decode/check.sh'), config_host['PYTHON'], files('../scripts/decodetree.py') ],
     workdir: meson.current_source_dir() / 'decode',
     suite: 'decodetree')

if 'CONFIG_TCG' in config_all
  subdir('fp')
endif

if not get_option('tcg').disabled()
  if 'CONFIG_PLUGIN' in config_host
    subdir('plugin')
  endif
endif

subdir('qapi-schema')
subdir('qtest')
subdir('migration')
