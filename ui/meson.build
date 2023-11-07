system_ss.add(pixman)
specific_ss.add(when: ['CONFIG_SYSTEM_ONLY'], if_true: pixman)   # for the include path
specific_ss.add(when: ['CONFIG_SYSTEM_ONLY'], if_true: opengl)   # for the include path

system_ss.add(png)
system_ss.add(files(
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
system_ss.add(when: pixman, if_true: files('console-vc.c'), if_false: files('console-vc-stubs.c'))
if dbus_display
  system_ss.add(files('dbus-module.c'))
endif
system_ss.add([spice_headers, files('spice-module.c')])
system_ss.add(when: spice_protocol, if_true: files('vdagent.c'))

system_ss.add(when: 'CONFIG_LINUX', if_true: files(
  'input-linux.c',
  'udmabuf.c',
))
system_ss.add(when: cocoa, if_true: files('cocoa.m'))

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
system_ss.add_all(when: [vnc, pixman], if_true: vnc_ss)
system_ss.add(when: vnc, if_false: files('vnc-stubs.c'))

ui_modules = {}

if curses.found()
  curses_ss = ss.source_set()
  curses_ss.add(when: [curses, iconv], if_true: [files('curses.c'), pixman])
  ui_modules += {'curses' : curses_ss}
endif

system_ss.add(opengl)
if opengl.found()
  opengl_ss = ss.source_set()
  opengl_ss.add(gbm, pixman)
  opengl_ss.add(when: [opengl],
               if_true: files('shader.c', 'console-gl.c', 'egl-helpers.c', 'egl-context.c'))
  ui_modules += {'opengl' : opengl_ss}
endif

if opengl.found()
  egl_headless_ss = ss.source_set()
  egl_headless_ss.add(when: [opengl, pixman],
                      if_true: [files('egl-headless.c'), gbm])
  ui_modules += {'egl-headless' : egl_headless_ss}
endif

if dbus_display
  dbus_ss = ss.source_set()
  env = environment()
  env.set('TARGETOS', targetos)
  xml = custom_target('dbus-display preprocess',
                      input: 'dbus-display1.xml',
                      output: 'dbus-display1.xml',
                      env: env,
                      command: [xml_pp, '@INPUT@', '@OUTPUT@'])
  dbus_display1 = custom_target('dbus-display gdbus-codegen',
                                output: ['dbus-display1.h', 'dbus-display1.c'],
                                input: xml,
                                command: [gdbus_codegen, '@INPUT@',
                                          '--glib-min-required', '2.64',
                                          '--output-directory', meson.current_build_dir(),
                                          '--interface-prefix', 'org.qemu.',
                                          '--c-namespace', 'QemuDBus',
                                          '--generate-c-code', '@BASENAME@'])
  dbus_display1_lib = static_library('dbus-display1', dbus_display1, dependencies: gio)
  dbus_display1_dep = declare_dependency(link_with: dbus_display1_lib, include_directories: include_directories('.'))
  dbus_ss.add(when: [gio, dbus_display1_dep],
              if_true: [files(
                'dbus-chardev.c',
                'dbus-clipboard.c',
                'dbus-console.c',
                'dbus-error.c',
                'dbus-listener.c',
                'dbus.c',
              ), opengl, gbm, pixman])
  ui_modules += {'dbus' : dbus_ss}
endif

if gtk.found()
  system_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

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
  system_ss.add(when: 'CONFIG_WIN32', if_true: files('win32-kbd-hook.c'))

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

  if gio.found()
    spice_ss = ss.source_set()
    spice_ss.add(spice, gio, pixman, files('spice-app.c'))
    ui_modules += {'spice-app': spice_ss}
  endif
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
  keycodemapdb_proj = subproject('keycodemapdb', required: true)
  foreach e : keymaps
    output = 'input-keymap-@0@-to-@1@.c.inc'.format(e[0], e[1])
    genh += custom_target(output,
                  output: output,
                  capture: true,
                  input: keycodemapdb_proj.get_variable('keymaps_csv'),
                  command: [python, keycodemapdb_proj.get_variable('keymap_gen').full_path(),
                            'code-map', '--lang', 'glib2',
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
