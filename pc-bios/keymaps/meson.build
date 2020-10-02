keymaps = {
  'ar': '-l ar',
  'bepo': '-l fr -v dvorak',
  'cz': '-l cz',
  'da': '-l dk',
  'de': '-l de -v nodeadkeys',
  'de-ch': '-l ch',
  'en-gb': '-l gb',
  'en-us': '-l us',
  'es': '-l es',
  'et': '-l et',
  'fi': '-l fi',
  'fo': '-l fo',
  'fr': '-l fr -v nodeadkeys',
  'fr-be': '-l be',
  'fr-ca': '-l ca -v fr',
  'fr-ch': '-l ch -v fr',
  'hr': '-l hr',
  'hu': '-l hu',
  'is': '-l is',
  'it': '-l it',
  'ja': '-l jp -m jp106',
  'lt': '-l lt',
  'lv': '-l lv',
  'mk': '-l mk',
  'nl': '-l nl',
  'no': '-l no',
  'pl': '-l pl',
  'pt': '-l pt',
  'pt-br': '-l br',
  'ru': '-l ru',
  'th': '-l th',
  'tr': '-l tr',
}

if meson.is_cross_build() or 'CONFIG_XKBCOMMON' not in config_host
  native_qemu_keymap = find_program('qemu-keymap', required: false, disabler: true)
else
  native_qemu_keymap = qemu_keymap
endif

t = []
foreach km, args: keymaps
  if native_qemu_keymap.found()
    # generate with qemu-kvm
    t += custom_target(km,
                       build_by_default: true,
                       output: km,
                       command: [native_qemu_keymap, '-f', '@OUTPUT@', args.split()],
                       install: true,
                       install_dir: qemu_datadir / 'keymaps')
  else
    # copy from source tree
    t += custom_target(km,
                       build_by_default: true,
                       input: km,
                       output: km,
                       command: ['cp', '@INPUT@', '@OUTPUT@'],
                       install: true,
                       install_dir: qemu_datadir / 'keymaps')
  endif
endforeach

if native_qemu_keymap.found()
  alias_target('update-keymaps', t)
endif

install_data(['sl', 'sv'], install_dir: qemu_datadir / 'keymaps')
