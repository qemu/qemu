qsd_qapi_files = custom_target('QAPI files for qemu-storage-daemon',
                               output: qapi_nonmodule_outputs,
                               input: [ files('qapi-schema.json') ],
                               command: [ qapi_gen, '-o', 'storage-daemon/qapi', '@INPUT@' ],
                               depend_files: [ qapi_inputs, qapi_gen_depends ])

qsd_ss.add(qsd_qapi_files.to_list())
