util_ss.add(files(
  'opts-visitor.c',
  'qapi-clone-visitor.c',
  'qapi-dealloc-visitor.c',
  'qapi-forward-visitor.c',
  'qapi-util.c',
  'qapi-visit-core.c',
  'qobject-input-visitor.c',
  'qobject-output-visitor.c',
  'string-input-visitor.c',
  'string-output-visitor.c',
))
if have_system
  util_ss.add(files('qapi-type-helpers.c'))
endif
if have_system or have_tools or have_ga
  util_ss.add(files(
    'qmp-dispatch.c',
    'qmp-event.c',
    'qmp-registry.c',
  ))
endif

qapi_all_modules = [
  'authz',
  'block',
  'block-core',
  'block-export',
  'char',
  'common',
  'compat',
  'control',
  'crypto',
  'cxl',
  'dump',
  'error',
  'introspect',
  'job',
  'machine',
  'machine-target',
  'migration',
  'misc',
  'misc-target',
  'net',
  'pragma',
  'qom',
  'replay',
  'run-state',
  'sockets',
  'stats',
  'trace',
  'transaction',
  'virtio',
  'yank',
]
if have_system
  qapi_all_modules += [
    'acpi',
    'audio',
    'cryptodev',
    'qdev',
    'pci',
    'rdma',
    'rocker',
    'tpm',
  ]
endif
if have_system or have_tools
  qapi_all_modules += [
    'ui',
  ]
endif

qapi_nonmodule_outputs = [
  'qapi-introspect.c', 'qapi-introspect.h',
  'qapi-types.c', 'qapi-types.h',
  'qapi-visit.h', 'qapi-visit.c',
  'qapi-commands.h', 'qapi-commands.c',
  'qapi-init-commands.h', 'qapi-init-commands.c',
  'qapi-events.h', 'qapi-events.c',
  'qapi-emit-events.c', 'qapi-emit-events.h',
]

# First build all sources
qapi_util_outputs = [
  'qapi-builtin-types.c', 'qapi-builtin-visit.c',
  'qapi-builtin-types.h', 'qapi-builtin-visit.h',
]

qapi_inputs = []
qapi_specific_outputs = []
foreach module : qapi_all_modules
  qapi_inputs += [ files(module + '.json') ]
  qapi_module_outputs = [
    'qapi-types-@0@.c'.format(module),
    'qapi-types-@0@.h'.format(module),
    'qapi-visit-@0@.c'.format(module),
    'qapi-visit-@0@.h'.format(module),
  ]
  if have_system or have_tools
    qapi_module_outputs += [
      'qapi-events-@0@.c'.format(module),
      'qapi-events-@0@.h'.format(module),
      'qapi-commands-@0@.c'.format(module),
      'qapi-commands-@0@.h'.format(module),
      'qapi-commands-@0@.trace-events'.format(module),
    ]
  endif
  if module.endswith('-target')
    qapi_specific_outputs += qapi_module_outputs
  else
    qapi_util_outputs += qapi_module_outputs
  endif
endforeach

qapi_files = custom_target('shared QAPI source files',
  output: qapi_util_outputs + qapi_specific_outputs + qapi_nonmodule_outputs,
  input: [ files('qapi-schema.json') ],
  command: [ qapi_gen, '-o', 'qapi', '-b', '@INPUT0@' ],
  depend_files: [ qapi_inputs, qapi_gen_depends ])

# Now go through all the outputs and add them to the right sourceset.
# These loops must be synchronized with the output of the above custom target.

i = 0
foreach output : qapi_util_outputs
  if output.endswith('.h')
    genh += qapi_files[i]
  endif
  if output.endswith('.trace-events')
    qapi_trace_events += qapi_files[i]
  endif
  util_ss.add(qapi_files[i])
  i = i + 1
endforeach

foreach output : qapi_specific_outputs + qapi_nonmodule_outputs
  if output.endswith('.h')
    genh += qapi_files[i]
  endif
  if output.endswith('.trace-events')
    qapi_trace_events += qapi_files[i]
  endif
  specific_ss.add(when: 'CONFIG_SYSTEM_ONLY', if_true: qapi_files[i])
  i = i + 1
endforeach
