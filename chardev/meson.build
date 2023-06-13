chardev_ss.add(files(
  'char-fe.c',
  'char-file.c',
  'char-io.c',
  'char-mux.c',
  'char-null.c',
  'char-pipe.c',
  'char-ringbuf.c',
  'char-serial.c',
  'char-socket.c',
  'char-stdio.c',
  'char-udp.c',
  'char.c',
))
chardev_ss.add(when: 'CONFIG_POSIX', if_true: [files(
  'char-fd.c',
  'char-pty.c',
), util])
if targetos in ['linux', 'gnu/kfreebsd', 'freebsd', 'dragonfly']
  chardev_ss.add(files('char-parallel.c'))
endif

chardev_ss.add(when: 'CONFIG_WIN32', if_true: files(
  'char-console.c',
  'char-win-stdio.c',
  'char-win.c',
))

chardev_ss = chardev_ss.apply(config_host, strict: false)

system_ss.add(files(
    'char-hmp-cmds.c',
    'msmouse.c',
    'wctablet.c',
    'testdev.c'))

chardev_modules = {}

if brlapi.found()
  module_ss = ss.source_set()
  module_ss.add(when: [brlapi], if_true: [files('baum.c'), pixman])
  chardev_modules += { 'baum': module_ss }
endif

if spice.found()
  module_ss = ss.source_set()
  module_ss.add(when: [spice], if_true: files('spice.c'))
  chardev_modules += { 'spice': module_ss }
endif

modules += { 'chardev': chardev_modules }
