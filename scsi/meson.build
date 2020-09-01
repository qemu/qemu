block_ss.add(files('utils.c'))
block_ss.add(when: 'CONFIG_LINUX',
             if_true: files('pr-manager.c', 'pr-manager-helper.c'),
             if_false: files('pr-manager-stub.c'))
