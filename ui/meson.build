keymaps = [
  ['atset1', 'linux'],
  ['linux', 'qcode'],
  ['linux', 'atset1'],
  ['linux', 'atset2'],
  ['linux', 'atset3'],
  ['qcode', 'linux'],
  ['linux', 'qnum'],
  ['linux', 'sun'],
  ['qnum', 'linux'],
  ['usb', 'linux'],
  ['win32', 'linux'],
  ['x11', 'linux'],
  ['xorgkbd', 'linux'],
  ['xorgxquartz', 'linux'],
  ['xorgxwin', 'linux'],
  ['osx', 'linux'],
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

libui_sources = files(
    'clipboard.c',
    'console.c',
    'cursor.c',
    'dmabuf.c',
    'display-surface.c',
    'input-keymap.c',
    'kbd-state.c',
    'keymaps.c',
    'qemu-pixman.c',
    'vgafont.c',
  )
if pixman.found()
  libui_sources += files('cp437.c', 'vt100.c')
endif
libui = static_library('qemuui', libui_sources + genh,
  dependencies: [pixman],
  build_by_default: false)
ui = declare_dependency(objects: libui.extract_all_objects(recursive: false), dependencies: [pixman])
system_ss.add(png)
system_ss.add(files(
  'input-legacy.c',
  'input-barrier.c',
  'input.c',
  'ui-hmp-cmds.c',
  'ui-qmp-cmds.c',
  'util.c',
))
system_ss.add(ui)
system_ss.add(when: pixman, if_true: files('console-vc.c'), if_false: files('console-vc-stubs.c'))
if dbus_display
  system_ss.add(files('dbus-module.c'))
endif
system_ss.add([spice_headers, files('spice-module.c')])
system_ss.add(when: spice_protocol, if_true: files('vdagent.c'))

if host_os == 'linux'
  system_ss.add(files('input-linux.c', 'udmabuf.c'))
endif
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
vnc_ss.add(zlib, jpeg, png)
vnc_ss.add(when: sasl, if_true: files('vnc-auth-sasl.c'))
system_ss.add_all(when: [vnc, pixman], if_true: vnc_ss)
system_ss.add(when: vnc, if_true: files('vnc-system.c'), if_false: files('vnc-stubs.c'))

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

dbus_display1 = files()
if dbus_display
  dbus_ss = ss.source_set()
  env = environment()
  env.set('HOST_OS', host_os)
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
  dbus_display1_h = declare_dependency(dependencies: [gio],
                                       sources: dbus_display1[0])
  dbus_ss.add(when: gio,
              if_true: [files(
                'dbus-chardev.c',
                'dbus-clipboard.c',
                'dbus-console.c',
                'dbus-error.c',
                'dbus-listener.c',
                'dbus.c',
              ), opengl, gbm, pixman, dbus_display1])
  ui_modules += {'dbus' : dbus_ss}
endif

if gtk.found()
  if host_os == 'windows'
    system_ss.add(files('win32-kbd-hook.c'))
  endif

  gtk_ss = ss.source_set()
  gtk_ss.add(gtk, vte, pixman, files('gtk.c'))
  gtk_ss.add(files('gtk-clipboard.c'))
  gtk_ss.add(when: x11, if_true: files('x_keymap.c'))
  gtk_ss.add(when: opengl, if_true: files('gtk-gl-area.c'))
  gtk_ss.add(when: [x11, opengl], if_true: files('gtk-egl.c'))
  ui_modules += {'gtk' : gtk_ss}
endif

if sdl.found()
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

subdir('shader')

if have_system
  subdir('icons')

  install_data('qemu.desktop', install_dir: qemu_desktopdir)
endif

modules += {'ui': ui_modules}
