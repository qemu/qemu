rustc = meson.get_compiler('rust')

if rustc.version().version_compare('<1.65.0')
  error('rustc version ' + rustc.version() + ' is unsupported. Please upgrade to at least 1.65.0')
endif
extra_args += [ '--cfg', 'std_backtrace' ] # >= 1.65.0
if rustc.version().version_compare('<1.81.0')
  extra_args += [ '--cfg', 'anyhow_no_core_error' ]
endif
