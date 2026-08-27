env = find_program('env')

gdb_progs = ['gdb-multiarch', 'gdb']
if config_host.has_key('GDB')
  gdb_progs = [config_host['GDB'], gdb_progs]
endif
gdb = find_program(gdb_progs, required: false)
prog_check_plugin_output = find_program('./scripts/check_plugin_output.sh')
prog_gdb_test = find_program('../guest-debug/run-test.py')
prog_record_replay = find_program('./scripts/record_replay.sh')
prog_run_and_diff = find_program('./scripts/run_and_diff.sh')
prog_run_with_input = find_program('./scripts/run_with_input.sh')
prog_run_and_check_forbidden_output = find_program(
  './scripts/run_and_check_forbidden_output.sh'
)

tcg_tests = {}
# tcg_tests is a dictionary with following structure:
#
# {
#   'name_of_target': {
#     'cc': cross_compiler,
#     'cc_feat_cflags': {
#       'name': ['feature cflags'],
#       ...
#     },
#     'folder': 'folder_for_test_src_files',
#     'gdb_arch': 'gdb architecture requirement',
#     'gdb_feat_version': {
#       'name': ['minimal gdb version supporting feature'],
#       ...
#     },
#     'qemu': emulators['qemu-...'],
#     'tests': [
#       {
#         'src_file': {
#           'exe_name': ['provide an alternative binary name'],
#           'test_name': ['provide an alternative test name'],
#           'cflags': ['additional cflags to compile test', ...],
#           'cc_feat': 'feature name, must be present in cc_feat_cflags',
#           'qemu_args': ['qemu command line flags', ...],
#           'expected_output': 'file_to_compare_output',
#           'gdb_test': ['gdb test args'],
#           'gdb_feat': ['feature name, must be present in gdb_feat_version'],
#           'env_var': ['VAR=value', ...],
#           'wrapper': [program_to_launch_test, 'args'...],
#           'plugin_test': {
#             'plugin': 'plugin_name',
#             'args': ['plugin_args', ...],
#           },
#         }
#       },
#       ...
#     ]
#   }
# }
#
# Every test executable, identified by 'exe_name' is built only once, and we
# ensure cflags are specified only once, on first declaration.
# Tests for a given src use the same executable by default, and their definition
# is guaranteed to be unique also. Several tests for the same executable can
# declared using 'test_name'.
# Default name is derived from src if 'exe_name' and 'test_name' are omitted.

gdb_arch_supported = {}
gdb_version = '0'
if gdb.found()
  gdb_version = gdb.version()
  gdb_arch = run_command(python, files('../../scripts/probe-gdb-support.py'),
                           gdb,
                           check: true)
  foreach arch: gdb_arch.stdout().split()
    gdb_arch_supported += {arch: true}
  endforeach
endif

# plugins come first, as we need to build the list
test_plugins = {}
subdir('plugins')

# Finally, we can create all test executables and test targets
foreach target, plan: tcg_tests
  # Detect duplicated executables/tests, and report an error to force user to
  # choose how to deal with it.
  built_tests = {}
  added_tests = {}
  cc_has_feat = {}

  # return a clear error if user misspell a target entry
  foreach key, _ : plan
    allowed = ['cc', 'cc_feat_cflags', 'folder', 'gdb_arch', 'gdb_feat_version',
               'qemu', 'tests']
    if key not in allowed
      error('unknown tcg test plan entry \'' + key + '\' for target ' + target +
            ' (possible: [' + ', '.join(allowed) + '])')
    endif
  endforeach

  cc = plan['cc']
  folder = plan['folder']
  gdb_arch = plan['gdb_arch']
  qemu = plan['qemu']
  cc_feat_cflags = {}
  if 'cc_feat_cflags' in plan
    cc_feat_cflags = plan['cc_feat_cflags']
  endif
  gdb_feat_version = {}
  if 'gdb_feat_version' in plan
    gdb_feat_version = plan['gdb_feat_version']
  endif

  tests = plan['tests']

  foreach t : tests
    foreach src, setup: t
      # return a clear error if user misspell a setup entry
      foreach key, _ : setup
        allowed = ['exe_name', 'env_var', 'expected_output', 'cc_feat', 'cflags',
                   'gdb_feat', 'gdb_test', 'plugin_test', 'qemu_args',
                   'test_name', 'wrapper']
        if key not in allowed
          error('unknown tcg setup entry \'' + key + '\' for test ' + src +
                ' (possible: [' + ', '.join(allowed) + '])')
        endif
      endforeach

      # return a clear error if user misspell a plugin entry
      if 'plugin_test' in setup
        foreach key, _ : setup['plugin_test']
          allowed = ['args', 'plugin']
          if key not in allowed
            error('unknown tcg plugin entry \'' + key + '\' for test ' + src +
                  ' (possible: [' + ', '.join(allowed) + '])')
          endif
        endforeach
      endif

      # if set, we'll skip test and print the reason
      skip_test = ''

      if 'plugin_test' in setup and not get_option('plugins')
        # do not use the skip_test logic, and simply ignore the test
        continue
      endif

      # meson '/' operator drops left operand if right is an absolute path
      src = folder / src
      file = files(src)
      exe_name = fs.stem(src)
      if 'exe_name' in setup
        exe_name = setup['exe_name']
      endif

      test_name = exe_name
      if 'test_name' in setup
        test_name = setup['test_name']
      endif
      if 'gdb_test' in setup
        test_name = 'gdb-' + test_name
      elif 'plugin_test' in setup
        plugin = test_plugins[setup['plugin_test']['plugin']]
        # remove 'lib' prefix
        plugin_name = fs.stem(plugin).substring(3)
        test_name = 'plugin-' + plugin_name + '-with-' + test_name
      endif

      exe_name = target + '-' + exe_name
      test_name = target + '-' + test_name

      if test_name in added_tests
        error('test ' + test_name + ' was already added: ' +
              'specify a different \'test_name\'')
      endif

      if 'cflags' in setup or 'cc_feat' in setup
        if exe_name in built_tests
          error('test ' + exe_name +
                ' was already built with cflags or cc_feat:' +
                ' remove cflags, cc_feat, or set \'exe_name\'' +
                ' (if cflags are different)')
        endif
      endif

      cflags = []
      if 'cflags' in setup
        cflags = setup['cflags']
      endif

      if 'cc_feat' in setup
        cc_feat = setup['cc_feat']
        if cc_feat not in cc_has_feat
          cmd = run_command([cc, cc_check_args, cc_feat_cflags[cc_feat]],
                            check: false)
          avail = cmd.returncode() == 0
          cc_has_feat += {cc_feat: avail}
        endif

        if not cc_has_feat[cc_feat]
          skip_test = 'compiler feature ' + cc_feat + ' not supported'
          built_tests += {exe_name: exe_name}
        endif
        cflags += cc_feat_cflags[cc_feat]
      endif

      # build executable if needed
      if exe_name not in built_tests
        exe = custom_target(exe_name,
                            input: file,
                            command: [cc, '@INPUT@',
                                      '-o', '@OUTPUT@', '-static',
                                      '-MMD', '-MF', '@DEPFILE@',
                                      '-Wall', '-Werror',
                                      '-O0', '-g', '-fno-strict-aliasing',
                                      cflags],
                            depfile: exe_name + '.d',
                            output: exe_name + '.test',
                            build_by_default: false)
        built_tests += {exe_name: exe}
      endif

      depends = []
      qemu_args = []
      if 'qemu_args' in setup
        qemu_args = setup['qemu_args']
      endif

      if 'plugin_test' in setup
        plugin_setup = setup['plugin_test']
        plugin = test_plugins[plugin_setup['plugin']]
        plugin_args = []
        if 'args' in plugin_setup
          # add empty arg to generate an additional comma
          plugin_args = ['', plugin_setup['args']]
        endif
        plugin_args = ','.join(plugin_args)

        # since we turn plugin into a string, add dependency explicitly
        depends += plugin
        qemu_args = ['-plugin', plugin.full_path() + plugin_args, qemu_args]
      endif

      exe = built_tests[exe_name]
      cmd = [qemu, qemu_args, exe]

      if 'gdb_test' in setup
        cmd = [prog_gdb_test,
               '--gdb', gdb,
               '--qemu', qemu,
               '--qargs', ' '.join(qemu_args),
               '--bin', exe,
               setup['gdb_test'],
              ]
      endif

      if 'expected_output' in setup
        expected = files(folder / setup['expected_output'])
        cmd = [prog_run_and_diff, expected, cmd]
      endif

      if 'wrapper' in setup
        cmd = [setup['wrapper'], cmd]
      endif

      # some tests expect QEMU env var to be set
      cmd = ['QEMU=' + qemu.full_path(), cmd]

      if 'env_var' in setup
        cmd = [setup['env_var'], cmd]
      endif

      if 'gdb_feat' in setup
        if 'gdb_test' not in setup
          error('test ' + test_name + ' is not a gdb test, but it requires ' +
                'a gdb feature')
        endif
        gdb_feat = setup['gdb_feat']

        if not gdb_version.version_compare('>=' + gdb_feat_version[gdb_feat])
          skip_test = 'gdb feature ' + gdb_feat + ' not supported'
        endif
      endif

      if 'gdb_test' in setup and gdb_arch not in gdb_arch_supported
        skip_test = 'gdb not available for ' + gdb_arch
      endif

      if skip_test != ''
        # still declare test, but skip it and print reason
        cmd = ['sh', '-c', 'echo "' + skip_test + '" && exit 77']
      endif

      test(test_name, env, args: cmd,
           depends: depends,
           timeout: 120,
           suite: ['tcg', 'tcg-' + target])
      added_tests += {test_name: true}
    endforeach
  endforeach
endforeach
