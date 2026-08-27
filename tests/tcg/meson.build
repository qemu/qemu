env = find_program('env')

docker_wrapper = find_program('../docker/docker.py')
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
#     'cc': 'cross_compiler',
#     'cc_dockerfile': 'name_of_dockerfile',
#     'cc_docker_host_arch': ['host_arch_supported (meson cpu)', ...],
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
#           'depends': [meson_dependency, ...],
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

docker_probe = run_command(docker_wrapper, 'probe', check: false)
docker_supported = docker_probe.returncode() == 0
if docker_supported
  docker_wrapper = [docker_wrapper, '--command', docker_probe.stdout().strip()]
endif

# plugins come first, as we need to build the list
test_plugins = {}
subdir('plugins')
# multiarch comes second, so it can be referenced by other arch after
subdir('multiarch')

# Generic Plugin tests
# add a single test for each plugin in user/system multiarch set
plugin_tests_exclude = ['patch']
foreach target: ['multiarch-linux-user', 'multiarch-softmmu']
  plugin_tests = []
  tests = tcg_tests[target]['tests']
  normal_tests = []

  # filter tests that are not plugin or gdb tests
  foreach t: tests
    foreach src, setup: t
      if 'plugin_test' in setup or 'gdb_test' in setup
        continue
      endif
      normal_tests += {src: setup}
    endforeach
  endforeach

  idx = 0
  foreach plugin_name, _: test_plugins
    if plugin_name in plugin_tests_exclude
      continue
    endif

    # pick one different test for each plugin
    foreach src, setup: normal_tests[idx % normal_tests.length()]
      # copy existing setup and remove existing cflags
      new_setup = {}
      foreach key, val: setup
        if key == 'cflags'
          continue
        endif
        new_setup += {key: val}
      endforeach
      new_setup += {'plugin_test': {'plugin': plugin_name}}
      plugin_tests += {src: new_setup}
    endforeach

    idx += 1
  endforeach

  # replace list of tests with old and new ones
  tcg_tests += {target: {'tests': tests + plugin_tests}}
endforeach

# Now let's go through all architectures
subdir('aarch64')
subdir('aarch64_be')
subdir('alpha')
subdir('arm')
subdir('hexagon')
subdir('hppa')
subdir('i386')
subdir('loongarch64')
subdir('m68k')
subdir('mips')
subdir('mips64')
subdir('mips64el')
subdir('or1k')
subdir('ppc64')
subdir('ppc64le')
subdir('riscv64')
subdir('s390x')
subdir('sh4')
subdir('tricore')
subdir('x86_64')
subdir('xtensa')

image_targets = {}
exe_targets = []
# Finally, we can create all test executables and test targets
foreach target, plan: tcg_tests
  if target.startswith('multiarch-')
    # those tests are included by architecture files if needed
    continue
  endif

  # Detect duplicated executables/tests, and report an error to force user to
  # choose how to deal with it.
  built_tests = {}
  added_tests = {}
  cc_has_feat = {}

  # return a clear error if user misspell a target entry
  foreach key, _ : plan
    allowed = ['cc', 'cc_dockerfile', 'cc_docker_host_arch', 'cc_feat_cflags',
               'folder', 'gdb_arch', 'gdb_feat_version', 'qemu', 'tests']
    if key not in allowed
      error('unknown tcg test plan entry \'' + key + '\' for target ' + target +
            ' (possible: [' + ', '.join(allowed) + '])')
    endif
  endforeach

  cc = find_program(plan['cc'], required : false)
  cc_from_system = cc.found()
  has_cc = cc.found()
  if has_cc
    check_flags = ['-static']
    if target.endswith('softmmu')
      check_flags = ['-nostdlib', '-ffreestanding', '-r']
    endif

    cmd = run_command([cc, files('test_cc.c'), check_flags, '-o', '/dev/null'],
                      check: false)
    has_cc = cmd.returncode() == 0
    cc_from_system = has_cc
  endif

  build_test_depends = []
  build_test_depend_files = []

  if 'cc_dockerfile' in plan
    cc_dockerfile = plan['cc_dockerfile']
    cc_docker_host_arch = plan['cc_docker_host_arch']
    allowed = ['aarch64', 'x86_64']
    foreach arch: cc_docker_host_arch
      if arch not in allowed
        error('incorrect docker arch ' + arch + ' for target ' +
              target + ' (possible: [' + ', '.join(allowed) + '])')
      endif
    endforeach
    docker_host_arch_supported = host_machine.cpu_family() in cc_docker_host_arch
    dockerfile = files('..'/'docker'/'dockerfiles'/cc_dockerfile + '.docker')
    image_name = 'image-' + cc_dockerfile
    has_docker = docker_host_arch_supported and docker_supported
    if cc_dockerfile not in image_targets and has_docker
      cmd = [docker_wrapper, 'build', '-f', dockerfile, '-t', cc_dockerfile]
      t = custom_target(image_name, command: cmd,
                        build_by_default: false,
                        output: 'no_output_' + image_name)
      image_targets += {cc_dockerfile: t}
    endif

    if not has_cc and has_docker
      build_test_depends = image_targets[cc_dockerfile]
      build_test_depend_files = dockerfile
      mount = meson.project_source_root()
      mount = mount + ':' + mount
      here = meson.project_build_root()
      cc = [docker_wrapper, 'run', '--run-as-current-user',
            '-w', here, '-v', mount,
            cc_dockerfile, plan['cc']]
      has_cc = true
    endif
  endif

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
                   'depends', 'gdb_feat', 'gdb_test', 'plugin_test', 'qemu_args',
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
          avail = false
          if cc_from_system
            # check flag for host cc
            cmd = run_command([cc, '-xc', '/dev/null', '-o', '/dev/null', '-c',
                               cc_feat_cflags[cc_feat]],
                              check: false)
            avail = cmd.returncode() == 0
          elif has_cc
            # docker cc always have feature
            avail = true
          endif
          cc_has_feat += {cc_feat: avail}
        endif

        if not cc_has_feat[cc_feat]
          skip_test = 'compiler feature ' + cc_feat + ' not supported'
          built_tests += {exe_name: exe_name}
        endif
        cflags += cc_feat_cflags[cc_feat]
      endif

      if not has_cc
        skip_test = 'compiler ' + plan['cc'] + ' not available'
        built_tests += {exe_name: exe_name}
      endif

      exe_depends = build_test_depends
      if 'depends' in setup
        exe_depends += setup['depends']
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
                            depends: exe_depends,
                            depend_files: build_test_depend_files,
                            build_by_default: false)
        exe_targets += exe
        built_tests += {exe_name: exe}
      endif

      test_depends = []
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
        test_depends += plugin
        qemu_args = ['-plugin', plugin.full_path() + plugin_args, qemu_args]
      endif

      exe = built_tests[exe_name]
      test_depends += qemu
      cmd = [qemu.full_path(), qemu_args, exe]

      if 'gdb_test' in setup
        cmd = [prog_gdb_test,
               '--gdb', gdb,
               '--qemu', qemu.full_path(),
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
           depends: test_depends,
           timeout: 120,
           suite: ['tcg', 'tcg-' + target])
      added_tests += {test_name: true}
    endforeach
  endforeach
endforeach

if exe_targets.length() > 0
  # finally create a top level target to rebuild all tests
  alias_target('tcg-tests', exe_targets)
endif
