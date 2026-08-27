env = find_program('env')

tcg_tests = {}
# tcg_tests is a dictionary with following structure:
#
# {
#   'name_of_target': {
#     'cc': cross_compiler,
#     'folder': 'folder_for_test_src_files',
#     'qemu': emulators['qemu-...'],
#     'tests': [
#       {
#         'src_file': {
#           'exe_name': ['provide an alternative binary name'],
#           'test_name': ['provide an alternative test name'],
#           'cflags': ['additional cflags to compile test', ...],
#           'qemu_args': ['qemu command line flags', ...],
#           'env_var': ['VAR=value', ...],
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

# plugins come first, as we need to build the list
test_plugins = {}
subdir('plugins')

# Finally, we can create all test executables and test targets
foreach target, plan: tcg_tests
  # Detect duplicated executables/tests, and report an error to force user to
  # choose how to deal with it.
  built_tests = {}
  added_tests = {}

  # return a clear error if user misspell a target entry
  foreach key, _ : plan
    allowed = ['cc', 'folder', 'qemu', 'tests']
    if key not in allowed
      error('unknown tcg test plan entry \'' + key + '\' for target ' + target +
            ' (possible: [' + ', '.join(allowed) + '])')
    endif
  endforeach

  cc = plan['cc']
  folder = plan['folder']
  qemu = plan['qemu']
  tests = plan['tests']

  foreach t : tests
    foreach src, setup: t
      # return a clear error if user misspell a setup entry
      foreach key, _ : setup
        allowed = ['exe_name', 'env_var', 'cflags', 'qemu_args', 'test_name']
        if key not in allowed
          error('unknown tcg setup entry \'' + key + '\' for test ' + src +
                ' (possible: [' + ', '.join(allowed) + '])')
        endif
      endforeach

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

      exe_name = target + '-' + exe_name
      test_name = target + '-' + test_name

      if test_name in added_tests
        error('test ' + test_name + ' was already added: ' +
              'specify a different \'test_name\'')
      endif

      cflags = []
      if 'cflags' in setup
        if exe_name in built_tests
          error('test ' + exe_name + ' was already built with cflags: ' +
                'remove cflags or set \'exe_name\' (if cflags are different)')
        endif
        cflags = setup['cflags']
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

      exe = built_tests[exe_name]
      cmd = [qemu, qemu_args, exe]

      # some tests expect QEMU env var to be set
      cmd = ['QEMU=' + qemu.full_path(), cmd]

      if 'env_var' in setup
        cmd = [setup['env_var'], cmd]
      endif

      test(test_name, env, args: cmd,
           depends: depends,
           timeout: 120,
           suite: ['tcg', 'tcg-' + target])
      added_tests += {test_name: true}
    endforeach
  endforeach
endforeach
