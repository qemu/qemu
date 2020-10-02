softmmu_ss.add(pixman)
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: pixman)   # for the include path

softmmu_ss.add(files(
  'console.c',
  'cursor.c',
  'input-keymap.c',
  'input-legacy.c',
  'input-barrier.c',
  'input.c',
  'kbd-state.c',
  'keymaps.c',
  'qemu-pixman.c',
))

softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files('input-linux.c'))
softmmu_ss.add(when: 'CONFIG_SPICE', if_true: files('spice-core.c', 'spice-input.c', 'spice-display.c'))
softmmu_ss.add(when: cocoa, if_true: files('cocoa.m'))

vnc_ss = ss.source_set()
vnc_ss.add(files(
  'vnc.c',
  'vnc-enc-zlib.c',
  'vnc-enc-hextile.c',
  'vnc-enc-tight.c',
  'vnc-palette.c',
  'vnc-enc-zrle.c',
  'vnc-auth-vencrypt.c',
  'vnc-ws.c',
  'vnc-jobs.c',
))
vnc_ss.add(zlib, png, jpeg)
vnc_ss.add(when: sasl, if_true: files('vnc-auth-sasl.c'))
softmmu_ss.add_all(when: vnc, if_true: vnc_ss)
softmmu_ss.add(when: vnc, if_false: files('vnc-stubs.c'))
softmmu_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('shader.c', 'console-gl.c', 'egl-helpers.c', 'egl-context.c'))
softmmu_ss.add(when: [opengl, 'CONFIG_OPENGL_DMABUF'], if_true: files('egl-headless.c'))
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: opengl)

ui_modules = {}

if config_host.has_key('CONFIG_CURSES')
  curses_ss = ss.source_set()
  curses_ss.add(when: [curses, iconv], if_true: [files('curses.c'), pixman])
  ui_modules += {'curses' : curses_ss}
endif

if config_host.has_key('CONFIG_GTK')
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

  gtk_ss = ss.source_set()
  gtk_ss.add(gtk, vte, pixman, files('gtk.c'))
  gtk_ss.add(when: [x11, 'CONFIG_X11'], if_true: files('x_keymap.c'))
  gtk_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('gtk-egl.c'))
  gtk_ss.add(when: [opengl, 'CONFIG_GTK_GL'], if_true: files('gtk-gl-area.c'))
  ui_modules += {'gtk' : gtk_ss}
endif

if sdl.found()
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

  sdl_ss = ss.source_set()
  sdl_ss.add(sdl, sdl_image, pixman, glib, files(
    'sdl2-2d.c',
    'sdl2-input.c',
    'sdl2.c',
  ))
  sdl_ss.add(when: [opengl, 'CONFIG_OPENGL'], if_true: files('sdl2-gl.c'))
  sdl_ss.add(when: [x11, 'CONFIG_X11'], if_true: files('x_keymap.c'))
  ui_modules += {'sdl' : sdl_ss}
endif

if config_host.has_key('CONFIG_SPICE') and config_host.has_key('CONFIG_GIO')
  spice_ss = ss.source_set()
  spice_ss.add(spice, gio, pixman, files('spice-app.c'))
  ui_modules += {'spice-app': spice_ss}
endif

keymap_gen = find_program('keycodemapdb/tools/keymap-gen')

keymaps = [
  ['atset1', 'qcode'],
  ['linux', 'qcode'],
  ['qcode', 'atset1'],
  ['qcode', 'atset2'],
  ['qcode', 'atset3'],
  ['qcode', 'linux'],
  ['qcode', 'qnum'],
  ['qcode', 'sun'],
  ['qnum', 'qcode'],
  ['usb', 'qcode'],
  ['win32', 'qcode'],
  ['x11', 'qcode'],
  ['xorgevdev', 'qcode'],
  ['xorgkbd', 'qcode'],
  ['xorgxquartz', 'qcode'],
  ['xorgxwin', 'qcode'],
  ['osx', 'qcode'],
]

if have_system or xkbcommon.found()
  foreach e : keymaps
    output = 'input-keymap-@0@-to-@1@.c.inc'.format(e[0], e[1])
    genh += custom_target(output,
                  output: output,
                  capture: true,
                  input: files('keycodemapdb/data/keymaps.csv'),
                  command: [python.full_path(), files('keycodemapdb/tools/keymap-gen'),
                            '--lang', 'glib2',
                            '--varname', 'qemu_input_map_@0@_to_@1@'.format(e[0], e[1]),
                            'code-map', '@INPUT0@', e[0], e[1]])
  endforeach
endif

subdir('shader')
subdir('icons')

install_data('qemu.desktop', install_dir: config_host['qemu_desktopdir'])

modules += {'ui': ui_modules}
