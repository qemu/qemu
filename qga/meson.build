qga_qapi_outputs = [
  'qga-qapi-commands.c',
  'qga-qapi-commands.h',
  'qga-qapi-doc.texi',
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
qga_ss.add(qga_qapi_files)
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
