plugin_list = run_command('cat', 'config.panda').stdout().strip().split('\n')

foreach plugin: plugin_list
    if plugin.strip() == '' or plugin.startswith('#')
        continue
    endif
    
    message('building plugin ' +plugin)
    mbuild = join_paths(plugin, 'meson.build')
    if fs.exists(mbuild)
        sources = []
        subdir(plugin)
        foreach target: target_dirs
            message('building plugin ' +plugin+ ' for target ' +target)
            # target = config_target['TARGET_NAME'] + '-softmmu'
            c_args = ['-DCOMPILING_PER_TARGET',
                    '-DCONFIG_TARGET="@0@-config-target.h"'.format(target),
                    '-DCONFIG_DEVICES="@0@-config-devices.h"'.format(target),
                    '-DPLUGIN_NAME='+plugin,
            ]
            build_dir = join_paths(meson.build_root(), 'build')
            # panda_dep = dependency('panda-'+target, required: true, 
                    # include_directories: include_directoriesbuild_dir])
            config_target = config_target_mak[target]
            link_args = ['-L'+build_dir, '-Lpanda-'+target]
            shared_library('panda-'+plugin+'_'+target, sources,
                c_args: c_args,
                link_args: link_args,
                include_directories: ['.', plugin, 
                    '../../include/', '../../include/panda', '../../build',
                    '../../target/'+ config_target['TARGET_BASE_ARCH']],
                    # dependencies: panda_dep)
                    override_options: ['b_lundef=false'])
        endforeach
    endif
endforeach