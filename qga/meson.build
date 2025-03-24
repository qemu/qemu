if not have_ga
  if get_option('guest_agent_msi').enabled()
    error('Guest agent MSI requested, but the guest agent is not being built')
  endif
  have_qga_vss = false
  subdir_done()
endif

have_qga_vss = get_option('qga_vss') \
  .require(host_os == 'windows',
           error_message: 'VSS support requires Windows') \
  .require('cpp' in all_languages,
           error_message: 'VSS support requires a C++ compiler') \
  .require(have_vss, error_message: '''VSS support requires VSS headers.
    If your Visual Studio installation doesn't have the VSS headers,
    Please download and install Microsoft VSS SDK:
    http://www.microsoft.com/en-us/download/details.aspx?id=23490
    On POSIX-systems, MinGW should provide headers in >=10.0 releases.
    you can extract the SDK headers by:
    $ scripts/extract-vsssdk-headers setup.exe
    The headers are extracted in the directory 'inc/win2003'.
    Then run configure with: --extra-cxxflags="-isystem /path/to/vss/inc/win2003"''') \
  .require(midl.found() or widl.found(),
           error_message: 'VSS support requires midl or widl') \
  .require(not get_option('prefer_static'),
           error_message: 'VSS support requires dynamic linking with GLib') \
  .allowed()

all_qga = []

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

# Problem: to generate trace events, we'd have to add the .trace-events
# file to qapi_trace_events like we do in qapi/meson.build.  Since
# qapi_trace_events is used by trace/meson.build, we'd have to move
# subdir('qga') above subdir('trace') in the top-level meson.build.
# Can't, because it would break the dependency of qga on qemuutil (which
# depends on trace_ss).  Not worth solving now; simply suppress trace
# event generation instead.
qga_qapi_files = custom_target('QGA QAPI files',
                               output: qga_qapi_outputs,
                               input: 'qapi-schema.json',
                               command: [ qapi_gen, '-o', 'qga', '-p', 'qga-', '@INPUT0@',
                                          '--suppress-tracing' ],
                               depend_files: qapi_gen_depends)

qga_ss = ss.source_set()
qga_ss.add(qga_qapi_files.to_list())
qga_ss.add(files(
  'commands.c',
  'guest-agent-command-state.c',
  'main.c',
  'cutils.c',
  'commands-common-ssh.c'
))
if host_os == 'windows'
  qga_ss.add(files(
    'channel-win32.c',
    'commands-win32.c',
    'service-win32.c',
    'vss-win32.c',
    'commands-windows-ssh.c'
  ))
else
  qga_ss.add(files(
    'channel-posix.c',
    'commands-posix.c',
    'commands-posix-ssh.c',
  ))
  if host_os == 'linux'
    qga_ss.add(files('commands-linux.c'))
  elif host_os in bsd_oses
    qga_ss.add(files('commands-bsd.c'))
  endif
endif

qga_ss = qga_ss.apply({})

gen_tlb = []
qga_libs = []
if host_os == 'windows'
  qga_libs += ['-lws2_32', '-lwinmm', '-lpowrprof', '-lwtsapi32', '-lwininet', '-liphlpapi', '-lnetapi32',
               '-lsetupapi', '-lcfgmgr32', '-luserenv', '-lpdh' ]
  if have_qga_vss
    qga_libs += ['-lole32', '-loleaut32', '-lshlwapi', '-lstdc++', '-Wl,--enable-stdcall-fixup']
    subdir('vss-win32')
  endif
endif

qga_objs = []
if host_os == 'windows'
  windmc = find_program('windmc', required: true)
  windres = find_program('windres', required: true)

  msgrc = custom_target('messages-win32.rc',
                        input: 'messages-win32.mc',
                        output: ['messages-win32.rc', 'MSG00409.bin', 'messages-win32.h'],
                        command: [windmc, '-h', '@OUTDIR@', '-r', '@OUTDIR@', '@INPUT@'])
  msgobj = custom_target('messages-win32.o',
                         input: msgrc[0],
                         output: 'messages-win32.o',
                         command: [windres, '-I', '@OUTDIR@', '-o', '@OUTPUT@', '@INPUT@'])

  qga_objs = [msgobj]
endif

qga = executable('qemu-ga', qga_ss.sources() + qga_objs,
                 link_args: qga_libs,
                 dependencies: [qemuutil, libudev],
                 install: true)
all_qga += qga

if host_os == 'windows'
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
    if have_qga_vss
      qemu_ga_msi_vss = ['-D', 'InstallVss']
      deps += qga_vss
    endif
    if glib.version().version_compare('<2.73.2')
      libpcre = 'libpcre1'
    else
      libpcre = 'libpcre2'
    endif
    qga_msi_version = get_option('qemu_ga_version') == '' \
      ? meson.project_version() \
      : get_option('qemu_ga_version')
    qga_msi = custom_target('QGA MSI',
                            input: files('installer/qemu-ga.wxs'),
                            output: 'qemu-ga-@0@.msi'.format(host_arch),
                            depends: deps,
                            command: [
                              wixl, '-o', '@OUTPUT0@', '@INPUT0@',
                              qemu_ga_msi_arch[cpu],
                              qemu_ga_msi_vss,
                              '-D', 'BUILD_DIR=' + meson.project_build_root(),
                              '-D', 'BIN_DIR=' + glib_pc.get_variable('bindir'),
                              '-D', 'QEMU_GA_VERSION=' + qga_msi_version,
                              '-D', 'QEMU_GA_MANUFACTURER=' + get_option('qemu_ga_manufacturer'),
                              '-D', 'QEMU_GA_DISTRO=' + get_option('qemu_ga_distro'),
                              '-D', 'LIBPCRE=' + libpcre,
                            ])
    all_qga += [qga_msi]
    alias_target('msi', qga_msi)
  endif
else
  if get_option('guest_agent_msi').enabled()
    error('MSI guest agent package is available only for MinGW Windows cross-compilation')
  endif
  install_emptydir(get_option('localstatedir') / 'run')
endif

alias_target('qemu-ga', all_qga)

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

# disable qga-ssh-test with fuzzing: glib's G_TEST_OPTION_ISOLATE_DIRS triggers
# the leak detector in build-oss-fuzz Gitlab CI test. we should re-enable
# this when an alternative is implemented or when the underlying glib
# issue is identified/fix
if host_os != 'windows' and not get_option('fuzzing')
  srcs = [files('commands-common-ssh.c', 'commands-posix-ssh.c')]
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
