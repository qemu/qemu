if 'CONFIG_LINUX' in config_host
    socket_scm_helper = executable('socket_scm_helper', 'socket_scm_helper.c')
else
    socket_scm_helper = []
endif
configure_file(output: 'common.env',
               input: files('common.env.in'),
               configuration: {'PYTHON': python.full_path()})
