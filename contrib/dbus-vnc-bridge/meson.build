# QEMU D-Bus VNC bridge: standalone VNC server coupled to QEMU via D-Bus
# Build only when -display dbus is enabled and on Unix (socket pair for Listener)

if not dbus_display
  subdir_done()
endif

# Unix only: RegisterListener uses Unix FD passing
if host_machine.system() == 'windows'
  subdir_done()
endif

# Reuse the same D-Bus interface definition as ui/
dbus_display_xml = meson.project_source_root() / 'ui' / 'dbus-display1.xml'
env = environment()
env.set('HOST_OS', host_machine.system())
dbus_xml_preprocessed = custom_target('dbus-vnc-bridge xml preprocess',
    input: dbus_display_xml,
    output: 'dbus-display1.xml',
    env: env,
    command: [xml_pp, '@INPUT@', '@OUTPUT@'])

dbus_display1 = custom_target('dbus-vnc-bridge gdbus-codegen',
    output: ['dbus-display1.h', 'dbus-display1.c'],
    input: dbus_xml_preprocessed,
    command: [gdbus_codegen, '@INPUT@',
              '--glib-min-required', '2.64',
              '--output-directory', meson.current_build_dir(),
              '--interface-prefix', 'org.qemu.',
              '--c-namespace', 'QemuDBus',
              '--generate-c-code', 'dbus-display1'])

executable('qemu-dbus-vnc-bridge',
    'main.c',
    'dbus-bridge.c',
    'vnc-server.c',
    dbus_display1,
    include_directories: include_directories('.'),
    dependencies: [gio],
    install: true,
    install_dir: get_option('bindir'))
