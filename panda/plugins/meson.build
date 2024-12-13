plugin_list = run_command('cat', 'config.panda').stdout().strip().split('\n')
add_languages('cpp', required: true)

build = '../../build'
root = '../../'

foreach plugin: plugin_list
    if plugin.strip() == '' or plugin.startswith('#')
        continue
    endif
    
    message('building plugin ' +plugin)
    
    c_args = [
        '-DCOMPILING_PER_TARGET',
    ]
        # join_paths(root, 'gdbstub'),
    includes = [
        include_directories(root),
        include_directories('.'),
        include_directories(join_paths(root, 'include')),
        include_directories(join_paths(root, 'include/panda')),
        include_directories(build),
        include_directories(plugin)
    ]

    link_args = [
        '-L'+build,
    ]
    

    # is this meson?
    if fs.exists(join_paths(plugin, 'meson.build'))
        sources = []
        subdir(plugin)
        foreach target: target_dirs
            message('building plugin @0@ for target @1@'.format(plugin, target))
            config_target = config_target_mak[target]
            target_c_args = [
                    '-DCONFIG_TARGET="@0@-config-target.h"'.format(target),
                    '-DCONFIG_DEVICES="@0@-config-devices.h"'.format(target),
                    '-DPLUGIN_NAME=@0@'.format(plugin),
            ]
            target_link_args = ['-Lpanda-'+target]
            target_includes = includes + [
                include_directories(join_paths(root, 'target', config_target['TARGET_BASE_ARCH'])),
                include_directories(join_paths(root, 'tcg', config_target['TARGET_BASE_ARCH'])),
            ]
            # foreach include: includes
                # debug(include)
            # endforeach
            shared_library('panda-@0@_@1@'.format(plugin, target), 
                sources,
                c_args: c_args + target_c_args,
                cpp_args: c_args + target_c_args,
                link_args: link_args + target_link_args,
                include_directories: target_includes,
                dependencies: [glib],
                override_options: ['b_lundef=false'])
        endforeach
    elif fs.exists(join_paths(plugin, 'Makefile'))
        foreach target: target_dirs
            message('building plugin ' +plugin+ ' with Makefile for target ' +target)
            config_target = config_target_mak[target]
            target_link_args = ['-Lpanda-'+target]
            target_includes = [
                '../../target/'+ config_target['TARGET_BASE_ARCH']
            ]
            target_c_args= [
                    '-DCONFIG_TARGET="@0@-config-target.h"'.format(target),
                    '-DCONFIG_DEVICES="@0@-config-devices.h"'.format(target),
                    '-DPLUGIN_NAME='+plugin,
            ]

            CFLAGS = ' '.join(c_args + target_c_args)
            INCLUDES = '-I' + ' -I'.join(includes + target_includes)
            LINK_ARGS = ' '.join(link_args + target_link_args)

            custom_target('make_panda_plugin_'+plugin+'_'+target, 
                capture: true,
                build_always_stale: true,
                build_by_default: true,
                output: ['libpanda-'+plugin+'_'+target+'.so'],
                command: ['make',
                'PLUGIN_NAME='+plugin,
                'PLUGIN_TARGET_DIR='+build_dir+'/panda/plugins',
                'PLUGIN_OBJ_DIR='+build_dir+'/panda/plugins/'+plugin,
                'CFLAGS="'+CFLAGS+'"',
                'INCLUDES="'+INCLUDES+'"',
                'LDFLAGS="'+LINK_ARGS+'"',            
                '-C', '../panda/plugins/'+plugin])
        endforeach
    endif
endforeach