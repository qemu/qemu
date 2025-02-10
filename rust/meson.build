subdir('qemu-api-macros')
subdir('qemu-api')

subdir('hw')

cargo = find_program('cargo', required: false)

if cargo.found()
  run_target('clippy',
    command: [config_host['MESON'], 'devenv',
              '--workdir', '@CURRENT_SOURCE_DIR@',
              cargo, 'clippy', '--tests'],
    depends: bindings_rs)

  run_target('rustfmt',
    command: [config_host['MESON'], 'devenv',
              '--workdir', '@CURRENT_SOURCE_DIR@',
              cargo, 'fmt'],
    depends: bindings_rs)

  run_target('rustdoc',
    command: [config_host['MESON'], 'devenv',
              '--workdir', '@CURRENT_SOURCE_DIR@',
              cargo, 'doc', '--no-deps', '--document-private-items'],
    depends: bindings_rs)
endif
