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
                               output: qga_qapi_outputs + ['qga-qapi-doc.texi'],
                               input: 'qapi-schema.json',
                               command: [ qapi_gen, '-o', 'qga', '-p', 'qga-', '@INPUT0@' ],
                               depend_files: qapi_gen_depends)

qga_ss = ss.source_set()
i = 0
foreach output: qga_qapi_outputs
  qga_ss.add(qga_qapi_files[i])
  i = i + 1
endforeach
qga_qapi_doc_texi = qga_qapi_files[i]

qga_ss.add(files(
  'commands.c',
  'guest-agent-command-state.c',
  'main.c',
))
qga_ss.add(when: 'CONFIG_POSIX', if_true: files(
  'channel-posix.c',
  'commands-posix.c'))
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

  wixl = find_program('wixl', required: false)
  if wixl.found()
    deps = [gen_tlb, qga]
    if 'CONFIG_QGA_VSS' in config_host and 'QEMU_GA_MSI_WITH_VSS' in config_host
      deps += qga_vss
    endif
    qga_msi = custom_target('QGA MSI',
                            input: files('installer/qemu-ga.wxs'),
                            output: 'qemu-ga-@0@.msi'.format(config_host['ARCH']),
                            depends: deps,
                            command: [
                              'QEMU_GA_VERSION=' + config_host['QEMU_GA_VERSION'],
                              'QEMU_GA_MANUFACTURER=' + config_host['QEMU_GA_MANUFACTURER'],
                              'QEMU_GA_DISTRO=' + config_host['QEMU_GA_DISTRO'],
                              'BUILD_DIR=' + meson.build_root(),
                              wixl, '-o', '@OUTPUT0@', '@INPUT0@',
                              config_host['QEMU_GA_MSI_ARCH'].split(),
                              config_host['QEMU_GA_MSI_WITH_VSS'].split(),
                              config_host['QEMU_GA_MSI_MINGW_DLL_PATH'].split(),
                            ])
    all_qga += [qga_msi]
    alias_target('msi', qga_msi)
  endif
else
  install_subdir('run', install_dir: get_option('localstatedir'))
endif

alias_target('qemu-ga', all_qga)
