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
#         }
#       },
#       ...
#     ]
#   }
# }
#
# Every test executable, is built only once.
# Tests for a given src use the same executable by default, and their definition
# is guaranteed to be unique also.
# Default name is derived from src.

# plugins come first, as we need to build the list
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
      # meson '/' operator drops left operand if right is an absolute path
      src = folder / src
      file = files(src)
      exe_name = fs.stem(src)

      exe_name = target + '-' + exe_name
      test_name = exe_name

      if test_name in added_tests
        error('test ' + test_name + ' was already added: ' +
              'specify a different \'test_name\'')
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
                                      ],
                            depfile: exe_name + '.d',
                            output: exe_name + '.test',
                            build_by_default: false)
        built_tests += {exe_name: exe}
      endif

      depends = []

      exe = built_tests[exe_name]
      cmd = [qemu, exe]

      # some tests expect QEMU env var to be set
      cmd = ['QEMU=' + qemu.full_path(), cmd]

      test(test_name, env, args: cmd,
           depends: depends,
           timeout: 120,
           suite: ['tcg', 'tcg-' + target])
      added_tests += {test_name: true}
    endforeach
  endforeach
endforeach
