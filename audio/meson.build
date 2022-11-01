softmmu_ss.add([spice_headers, files('audio.c')])
softmmu_ss.add(files(
  'audio_legacy.c',
  'mixeng.c',
  'noaudio.c',
  'wavaudio.c',
  'wavcapture.c',
))

softmmu_ss.add(when: coreaudio, if_true: files('coreaudio.m'))
softmmu_ss.add(when: dsound, if_true: files('dsoundaudio.c', 'audio_win_int.c'))

audio_modules = {}
foreach m : [
  ['alsa', alsa, files('alsaaudio.c')],
  ['oss', oss, files('ossaudio.c')],
  ['pa', pulse, files('paaudio.c')],
  ['sdl', sdl, files('sdlaudio.c')],
  ['jack', jack, files('jackaudio.c')],
  ['sndio', sndio, files('sndioaudio.c')],
  ['spice', spice, files('spiceaudio.c')]
]
  if m[1].found()
    module_ss = ss.source_set()
    module_ss.add(m[1], m[2])
    audio_modules += {m[0] : module_ss}
  endif
endforeach

if dbus_display
    module_ss = ss.source_set()
    module_ss.add(when: gio, if_true: files('dbusaudio.c'))
    audio_modules += {'dbus': module_ss}
endif

modules += {'audio': audio_modules}
