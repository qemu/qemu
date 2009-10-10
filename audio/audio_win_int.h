#ifndef AUDIO_WIN_INT_H
#define AUDIO_WIN_INT_H

int waveformat_from_audio_settings (WAVEFORMATEX *wfx,
                                    struct audsettings *as);

int waveformat_to_audio_settings (WAVEFORMATEX *wfx,
                                  struct audsettings *as);

#endif /* AUDIO_WIN_INT_H */
