if have_pvrdma
  # FIXME: broken on big endian architectures
  executable('rdmacm-mux', files('main.c'), genh,
             dependencies: [glib, libumad],
             build_by_default: false,
             install: false)
endif
