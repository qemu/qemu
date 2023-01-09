softmmu_ss.add(pixman)
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: pixman)   # for the include path
specific_ss.add(when: ['CONFIG_SOFTMMU'], if_true: opengl)   # for the include path

softmmu_ss.add(png)
softmmu_ss.add(files(
  'clipboard.c',
  'console.c',
  'cursor.c',
  'input-keymap.c',
  'input-legacy.c',
  'input-barrier.c',
  'input.c',
  'kbd-state.c',
  'keymaps.c',
  'qemu-pixman.c',
  'ui-hmp-cmds.c',
  'ui-qmp-cmds.c',
  'util.c',
))
if dbus_display
  softmmu_ss.add(files('dbus-module.c'))
endif
softmmu_ss.add([spice_headers, files('spice-module.c')])
softmmu_ss.add(when: spice_protocol, if_true: files('vdagent.c'))

softmmu_ss.add(when: 'CONFIG_LINUX', if_true: files(
  'input-linux.c',
  'udmabuf.c',
))
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
  'vnc-clipboard.c',
))
vnc_ss.add(zlib, jpeg, gnutls)
vnc_ss.add(when: sasl, if_true: files('vnc-auth-sasl.c'))
softmmu_ss.add_all(when: vnc, if_true: vnc_ss)
softmmu_ss.add(when: vnc, if_false: files('vnc-stubs.c'))

ui_modules = {}

if curses.found()
  curses_ss = ss.source_set()
  curses_ss.add(when: [curses, iconv], if_true: [files('curses.c'), pixman])
  ui_modules += {'curses' : curses_ss}
endif

softmmu_ss.add(opengl)
if opengl.found()
  opengl_ss = ss.source_set()
  opengl_ss.add(gbm)
  opengl_ss.add(when: [opengl, pixman],
               if_true: files('shader.c', 'console-gl.c', 'egl-helpers.c', 'egl-context.c'))
  ui_modules += {'opengl' : opengl_ss}
endif

if opengl.found() and gbm.found()
  egl_headless_ss = ss.source_set()
  egl_headless_ss.add(when: [opengl, gbm, pixman],
                      if_true: files('egl-headless.c'))
  ui_modules += {'egl-headless' : egl_headless_ss}
endif

if dbus_display
  dbus_ss = ss.source_set()
  dbus_display1 = custom_target('dbus-display gdbus-codegen',
                                output: ['dbus-display1.h', 'dbus-display1.c'],
                                input: files('dbus-display1.xml'),
                                command: [gdbus_codegen, '@INPUT@',
                                          '--glib-min-required', '2.64',
                                          '--output-directory', meson.current_build_dir(),
                                          '--interface-prefix', 'org.qemu.',
                                          '--c-namespace', 'QemuDBus',
                                          '--generate-c-code', '@BASENAME@'])
  dbus_ss.add(when: [gio, pixman, opengl, gbm],
              if_true: [files(
                'dbus-chardev.c',
                'dbus-clipboard.c',
                'dbus-console.c',
                'dbus-error.c',
                'dbus-listener.c',
                'dbus.c',
              ), dbus_display1])
  ui_modules += {'dbus' : dbus_ss}
endif

if gtk.found()
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

  gtk_ss = ss.source_set()
  gtk_ss.add(gtk, vte, pixman, files('gtk.c'))
  if have_gtk_clipboard
    gtk_ss.add(files('gtk-clipboard.c'))
  endif
  gtk_ss.add(when: x11, if_true: files('x_keymap.c'))
  gtk_ss.add(when: opengl, if_true: files('gtk-gl-area.c'))
  gtk_ss.add(when: [x11, opengl], if_true: files('gtk-egl.c'))
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
  sdl_ss.add(when: opengl, if_true: files('sdl2-gl.c'))
  sdl_ss.add(when: x11, if_true: files('x_keymap.c'))
  ui_modules += {'sdl' : sdl_ss}
endif

if spice.found()
  spice_core_ss = ss.source_set()
  spice_core_ss.add(spice, pixman, files(
    'spice-core.c',
    'spice-input.c',
    'spice-display.c'
  ))
  ui_modules += {'spice-core' : spice_core_ss}
endif

if spice.found() and gio.found()
  spice_ss = ss.source_set()
  spice_ss.add(spice, gio, pixman, files('spice-app.c'))
  ui_modules += {'spice-app': spice_ss}
endif

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
                  command: [python, files('keycodemapdb/tools/keymap-gen'),
                            'code-map',
                            '--lang', 'glib2',
                            '--varname', 'qemu_input_map_@0@_to_@1@'.format(e[0], e[1]),
                            '@INPUT0@', e[0], e[1]])
  endforeach
endif

subdir('shader')

if have_system
  subdir('icons')

  install_data('qemu.desktop', install_dir: qemu_desktopdir)
endif

modules += {'ui': ui_modules}
