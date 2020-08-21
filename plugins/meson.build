if 'CONFIG_HAS_LD_DYNAMIC_LIST' in config_host
  plugin_ldflags = ['-Wl,--dynamic-list=' + (meson.build_root() / 'qemu-plugins-ld.symbols')]
elif 'CONFIG_HAS_LD_EXPORTED_SYMBOLS_LIST' in config_host
  plugin_ldflags = ['-Wl,-exported_symbols_list,' + (meson.build_root() / 'qemu-plugins-ld64.symbols')]
else
  plugin_ldflags = []
endif

specific_ss.add(when: 'CONFIG_PLUGIN', if_true: [files(
  'loader.c',
  'core.c',
  'api.c',
), declare_dependency(link_args: plugin_ldflags)])
