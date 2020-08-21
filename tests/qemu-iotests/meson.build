if 'CONFIG_LINUX' in config_host
    socket_scm_helper = executable('socket_scm_helper', 'socket_scm_helper.c',
                                    build_by_default: false)
else
    socket_scm_helper = []
endif
test('qemu-iotests', sh, args: [files('../check-block.sh')],
     depends: [qemu_block_tools, emulators, socket_scm_helper],
     suite: 'block', timeout: 10000)

