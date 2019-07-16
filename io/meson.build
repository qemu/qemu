io_ss = ss.source_set()
io_ss.add(genh)
io_ss.add(files(
  'channel-buffer.c',
  'channel-command.c',
  'channel-file.c',
  'channel-socket.c',
  'channel-tls.c',
  'channel-util.c',
  'channel-watch.c',
  'channel-websock.c',
  'channel.c',
  'dns-resolver.c',
  'net-listener.c',
  'task.c',
))

io_ss = io_ss.apply(config_host, strict: false)
libio = static_library('io', io_ss.sources() + genh,
                       dependencies: [io_ss.dependencies()],
                       link_with: libqemuutil,
                       name_suffix: 'fa',
                       build_by_default: false)

io = declare_dependency(link_whole: libio, dependencies: [crypto, qom])
