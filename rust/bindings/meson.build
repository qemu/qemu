# Generate bindgen arguments from Cargo.toml metadata
# Sort these in dependency order, same as the subdir()
# invocations below.
bindgen_dirs = [
  'util-sys',
  'migration-sys',
  'qom-sys',
  'chardev-sys',
  'hwcore-sys',
  'system-sys',
]
bindgen_args_file = configure_file(
  command: [files('generate_bindgen_args.py'),
            '-I', meson.project_source_root() / 'include',
            '--source-dir', meson.current_source_dir(),
            '-o', '@OUTPUT@', '--dep-file', '@DEPFILE@'] + bindgen_dirs,
  output: 'bindgen_args.mak',
  depfile: 'bindgen_args.d'
)

# now generate all bindgen files
bindgen_args_data = keyval.load(bindgen_args_file)
bindgen_kwargs = {
  'input': 'wrapper.h',
  'dependencies': common_ss.all_dependencies(),
  'output': 'bindings.inc.rs',
  'include_directories': bindings_incdir,
  'bindgen_version': ['>=0.60.0'],
  'c_args': bindgen_c_args,
}

subdir('util-sys')
subdir('migration-sys')
subdir('qom-sys')
subdir('chardev-sys')
subdir('hwcore-sys')
subdir('system-sys')
