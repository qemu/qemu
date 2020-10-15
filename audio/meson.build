softmmu_ss.add([spice_headers, files('audio.c')])
softmmu_ss.add(files(
  'audio_legacy.c',
  'mixeng.c',
  'noaudio.c',
  'wavaudio.c',
  'wavcapture.c',
))

softmmu_ss.add(when: [coreaudio, 'CONFIG_AUDIO_COREAUDIO'], if_true: files('coreaudio.c'))
softmmu_ss.add(when: [dsound, 'CONFIG_AUDIO_DSOUND'], if_true: files('dsoundaudio.c'))
softmmu_ss.add(when: ['CONFIG_AUDIO_WIN_INT'], if_true: files('audio_win_int.c'))

audio_modules = {}
foreach m : [
  ['CONFIG_AUDIO_ALSA', 'alsa', alsa, 'alsaaudio.c'],
  ['CONFIG_AUDIO_OSS', 'oss', oss, 'ossaudio.c'],
  ['CONFIG_AUDIO_PA', 'pa', pulse, 'paaudio.c'],
  ['CONFIG_AUDIO_SDL', 'sdl', sdl, 'sdlaudio.c'],
  ['CONFIG_AUDIO_JACK', 'jack', jack, 'jackaudio.c'],
  ['CONFIG_SPICE', 'spice', spice, 'spiceaudio.c']
]
  if config_host.has_key(m[0])
    module_ss = ss.source_set()
    module_ss.add(when: m[2], if_true: files(m[3]))
    audio_modules += {m[1] : module_ss}
  endif
endforeach

modules += {'audio': audio_modules}
