# an extra target to workaround meson limitation on output files location
test_qapi_outputs_extra = [
  'test-qapi-commands-sub-module.c',
  'test-qapi-commands-sub-module.h',
  'test-qapi-events-sub-module.c',
  'test-qapi-events-sub-module.h',
  'test-qapi-types-sub-module.c',
  'test-qapi-types-sub-module.h',
  'test-qapi-visit-sub-module.c',
  'test-qapi-visit-sub-module.h',
]

test_qapi_files_extra = custom_target('QAPI test (include)',
                                      output: test_qapi_outputs_extra,
                                      input: test_qapi_files,
                                      command: [python, '-c', ''])
