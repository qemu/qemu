system_ss.add(when: 'CONFIG_TCG', if_true: files(
  'replay.c',
  'replay-internal.c',
  'replay-events.c',
  'replay-time.c',
  'replay-input.c',
  'replay-char.c',
  'replay-snapshot.c',
  'replay-net.c',
  'replay-audio.c',
  'replay-random.c',
  'replay-debugging.c',
), if_false: files('stubs-system.c'))
