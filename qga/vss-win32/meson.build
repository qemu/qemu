link_args = cc.get_supported_link_arguments([
  '-fstack-protector-all',
  '-fstack-protector-strong',
  '-Wl,--add-stdcall-alias',
  '-Wl,--enable-stdcall-fixup'
])

qga_vss = shared_module(
  'qga-vss',
  ['requester.cpp', 'provider.cpp', 'install.cpp'],
  name_prefix: '',
  cpp_args: ['-Wno-unknown-pragmas', '-Wno-delete-non-virtual-dtor', '-Wno-non-virtual-dtor'],
  link_args: link_args,
  vs_module_defs: 'qga-vss.def',
  dependencies: [
    glib,
    socket,
    cc.find_library('ole32'),
    cc.find_library('oleaut32'),
    cc.find_library('shlwapi'),
    cc.find_library('uuid'),
    cc.find_library('intl')
  ]
)

all_qga += qga_vss

if midl.found()
  gen_tlb = custom_target('gen-tlb',
                          input: 'qga-vss.idl',
                          output: 'qga-vss.tlb',
                          command: [midl, '@INPUT@', '/tlb', '@OUTPUT@'])
else
  gen_tlb = custom_target('gen-tlb',
                          input: 'qga-vss.idl',
                          output: 'qga-vss.tlb',
                          command: [widl, '-t', '@INPUT@', '-o', '@OUTPUT@'])
endif
