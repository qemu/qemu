if get_option('sphinx_build') == ''
  sphinx_build = find_program(['sphinx-build-3', 'sphinx-build'],
                              required: get_option('docs'))
else
  sphinx_build = find_program(get_option('sphinx_build'),
                              required: get_option('docs'))
endif

# Check if tools are available to build documentation.
build_docs = false
if sphinx_build.found()
  SPHINX_ARGS = ['env', 'CONFDIR=' + qemu_confdir, sphinx_build, '-q']
  # If we're making warnings fatal, apply this to Sphinx runs as well
  if get_option('werror')
    SPHINX_ARGS += [ '-W' ]
  endif

  # This is a bit awkward but works: create a trivial document and
  # try to run it with our configuration file (which enforces a
  # version requirement). This will fail if sphinx-build is too old.
  run_command('mkdir', ['-p', tmpdir / 'sphinx'], check: true)
  run_command('touch', [tmpdir / 'sphinx/index.rst'], check: true)
  sphinx_build_test_out = run_command(SPHINX_ARGS + [
    '-c', meson.current_source_dir(),
    '-b', 'html', tmpdir / 'sphinx',
    tmpdir / 'sphinx/out'], check: false)
  build_docs = (sphinx_build_test_out.returncode() == 0)

  if not build_docs
    warning('@0@: @1@'.format(sphinx_build.full_path(), sphinx_build_test_out.stderr()))
    if get_option('docs').enabled()
      error('Install a Python 3 version of python-sphinx and the readthedoc theme')
    endif
  endif
endif

if build_docs
  SPHINX_ARGS += ['-Dversion=' + meson.project_version(), '-Drelease=' + config_host['PKGVERSION']]

  man_pages = {
        'qemu-ga.8': (have_ga ? 'man8' : ''),
        'qemu-ga-ref.7': (have_ga ? 'man7' : ''),
        'qemu-qmp-ref.7': 'man7',
        'qemu-storage-daemon-qmp-ref.7': (have_tools ? 'man7' : ''),
        'qemu-img.1': (have_tools ? 'man1' : ''),
        'qemu-nbd.8': (have_tools ? 'man8' : ''),
        'qemu-pr-helper.8': (have_tools ? 'man8' : ''),
        'qemu-storage-daemon.1': (have_tools ? 'man1' : ''),
        'qemu-trace-stap.1': (stap.found() ? 'man1' : ''),
        'virtfs-proxy-helper.1': (have_virtfs_proxy_helper ? 'man1' : ''),
        'virtiofsd.1': (have_virtiofsd ? 'man1' : ''),
        'qemu.1': 'man1',
        'qemu-block-drivers.7': 'man7',
        'qemu-cpu-models.7': 'man7'
  }

  sphinxdocs = []
  sphinxmans = []

  private_dir = meson.current_build_dir() / 'manual.p'
  output_dir = meson.current_build_dir() / 'manual'
  input_dir = meson.current_source_dir()

  this_manual = custom_target('QEMU manual',
                build_by_default: build_docs,
                output: 'docs.stamp',
                input: files('conf.py'),
                depfile: 'docs.d',
                command: [SPHINX_ARGS, '-Ddepfile=@DEPFILE@',
                          '-Ddepfile_stamp=@OUTPUT0@',
                          '-b', 'html', '-d', private_dir,
                          input_dir, output_dir])
  sphinxdocs += this_manual
  install_subdir(output_dir, install_dir: qemu_docdir, strip_directory: true)

  these_man_pages = []
  install_dirs = []
  foreach page, section : man_pages
    these_man_pages += page
    install_dirs += section == '' ? false : get_option('mandir') / section
  endforeach

  sphinxmans += custom_target('QEMU man pages',
                              build_by_default: build_docs,
                              output: these_man_pages,
                              input: this_manual,
                              install: build_docs,
                              install_dir: install_dirs,
                              command: [SPHINX_ARGS, '-b', 'man', '-d', private_dir,
                                        input_dir, meson.current_build_dir()])

  alias_target('sphinxdocs', sphinxdocs)
  alias_target('html', sphinxdocs)
  alias_target('man', sphinxmans)
endif
