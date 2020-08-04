test('decodetree', sh,
     args: [ files('decode/check.sh'), config_host['PYTHON'], files('../scripts/decodetree.py') ],
     workdir: meson.current_source_dir() / 'decode',
     suite: 'decodetree')

if 'CONFIG_TCG' in config_host
  subdir('fp')
endif

subdir('qapi-schema')
subdir('qtest')
