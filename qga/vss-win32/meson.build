if add_languages('cpp', required: false)
  glib_dynamic = dependency('glib-2.0', static: false)
  link_args = cc.get_supported_link_arguments(['-fstack-protector-all', '-fstack-protector-strong',
                                               '-Wl,--add-stdcall-alias', '-Wl,--enable-stdcall-fixup'])

  qga_vss = shared_module('qga-vss', ['requester.cpp', 'provider.cpp', 'install.cpp'],
                name_prefix: '',
                cpp_args: ['-Wno-unknown-pragmas', '-Wno-delete-non-virtual-dtor', '-Wno-non-virtual-dtor'],
                link_args: link_args,
                vs_module_defs: 'qga-vss.def',
                dependencies: [glib_dynamic, socket,
                               cc.find_library('ole32'),
                               cc.find_library('oleaut32'),
                               cc.find_library('shlwapi'),
                               cc.find_library('uuid'),
                               cc.find_library('intl')])

  all_qga += qga_vss
endif

# rules to build qga-vss.tlb
# Currently, only native build is supported because building .tlb
# (TypeLibrary) from .idl requires WindowsSDK and MIDL (and cl.exe in VC++).
midl = find_program('midl', required: false)
if midl.found()
  gen_tlb = custom_target('gen-tlb',
                          input: 'qga-vss.idl',
                          output: 'qga-vss.tlb',
                          command: [midl, '-tlb', '-I' + config_host['WIN_SDK'],
                                     '@INPUT@', '@OUTPUT@'])
else
  gen_tlb = custom_target('gen-tlb',
                          input: 'qga-vss.tlb',
                          output: 'qga-vss.tlb',
                          command: ['cp', '@INPUT@', '@OUTPUT@'])
endif
