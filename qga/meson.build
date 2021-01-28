qga_qapi_outputs = [
  'qga-qapi-commands.c',
  'qga-qapi-commands.h',
  'qga-qapi-emit-events.c',
  'qga-qapi-emit-events.h',
  'qga-qapi-events.c',
  'qga-qapi-events.h',
  'qga-qapi-init-commands.c',
  'qga-qapi-init-commands.h',
  'qga-qapi-introspect.c',
  'qga-qapi-introspect.h',
  'qga-qapi-types.c',
  'qga-qapi-types.h',
  'qga-qapi-visit.c',
  'qga-qapi-visit.h',
]

qga_qapi_files = custom_target('QGA QAPI files',
                               output: qga_qapi_outputs,
                               input: 'qapi-schema.json',
                               command: [ qapi_gen, '-o', 'qga', '-p', 'qga-', '@INPUT0@' ],
                               depend_files: qapi_gen_depends)

qga_ss = ss.source_set()
qga_ss.add(qga_qapi_files.to_list())
qga_ss.add(files(
  'commands.c',
  'guest-agent-command-state.c',
  'main.c',
))
qga_ss.add(when: 'CONFIG_POSIX', if_true: files(
  'channel-posix.c',
  'commands-posix.c',
  'commands-posix-ssh.c',
))
qga_ss.add(when: 'CONFIG_WIN32', if_true: files(
  'channel-win32.c',
  'commands-win32.c',
  'service-win32.c',
  'vss-win32.c'
))

qga_ss = qga_ss.apply(config_host, strict: false)

qga = executable('qemu-ga', qga_ss.sources(),
                 link_args: config_host['LIBS_QGA'].split(),
                 dependencies: [qemuutil, libudev],
                 install: true)
all_qga = [qga]

if targetos == 'windows'
  if 'CONFIG_QGA_VSS' in config_host
    subdir('vss-win32')
  else
    gen_tlb = []
  endif

  qemu_ga_msi_arch = {
    'x86': ['-D', 'Arch=32'],
    'x86_64': ['-a', 'x64', '-D', 'Arch=64']
  }
  wixl = not_found
  if cpu in qemu_ga_msi_arch
    wixl = find_program('wixl', required: get_option('guest_agent_msi'))
  elif get_option('guest_agent_msi').enabled()
    error('CPU not supported for building guest agent installation package')
  endif

  if wixl.found()
    deps = [gen_tlb, qga]
    qemu_ga_msi_vss = []
    if 'CONFIG_QGA_VSS' in config_host
      qemu_ga_msi_vss = ['-D', 'InstallVss']
      deps += qga_vss
    endif
    qga_msi = custom_target('QGA MSI',
                            input: files('installer/qemu-ga.wxs'),
                            output: 'qemu-ga-@0@.msi'.format(config_host['ARCH']),
                            depends: deps,
                            command: [
                              find_program('env'),
                              'QEMU_GA_VERSION=' + config_host['QEMU_GA_VERSION'],
                              'QEMU_GA_MANUFACTURER=' + config_host['QEMU_GA_MANUFACTURER'],
                              'QEMU_GA_DISTRO=' + config_host['QEMU_GA_DISTRO'],
                              'BUILD_DIR=' + meson.build_root(),
                              wixl, '-o', '@OUTPUT0@', '@INPUT0@',
                              qemu_ga_msi_arch[cpu],
                              qemu_ga_msi_vss,
                              '-D', 'Mingw_dlls=' + config_host['QEMU_GA_MSI_MINGW_DLL_PATH'],
                            ])
    all_qga += [qga_msi]
    alias_target('msi', qga_msi)
  endif
else
  if get_option('guest_agent_msi').enabled()
    error('MSI guest agent package is available only for MinGW Windows cross-compilation')
  endif
  install_subdir('run', install_dir: get_option('localstatedir'))
endif

alias_target('qemu-ga', all_qga)

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

# disable qga-ssh-test for now. glib's G_TEST_OPTION_ISOLATE_DIRS triggers
# the leak detector in build-oss-fuzz Gitlab CI test. we should re-enable
# this when an alternative is implemented or when the underlying glib
# issue is identified/fix
#if 'CONFIG_POSIX' in config_host
if false
  srcs = [files('commands-posix-ssh.c')]
  i = 0
  foreach output: qga_qapi_outputs
    if output.startswith('qga-qapi-types') or output.startswith('qga-qapi-visit')
      srcs += qga_qapi_files[i]
    endif
    i = i + 1
  endforeach
  qga_ssh_test = executable('qga-ssh-test', srcs,
                            dependencies: [qemuutil],
                            c_args: ['-DQGA_BUILD_UNIT_TEST'])

  test('qga-ssh-test',
       qga_ssh_test,
       env: test_env,
       suite: ['unit', 'qga'])
endif
