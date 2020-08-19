qsd_qapi_files = custom_target('QAPI files for qemu-storage-daemon',
                               output: qapi_nonmodule_outputs + ['qapi-doc.texi'],
                               input: [ files('qapi-schema.json') ],
                               command: [ qapi_gen, '-o', 'storage-daemon/qapi', '@INPUT@' ],
                               depend_files: [ qapi_inputs, qapi_gen_depends ])

static_library('qsd-qapi',
               qsd_qapi_files.to_list(),
               name_suffix: 'fa',
               build_by_default: false)
