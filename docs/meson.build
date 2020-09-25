SPHINX_ARGS = [config_host['SPHINX_BUILD'],
               '-Dversion=' + meson.project_version(),
               '-Drelease=' + config_host['PKGVERSION']]

if get_option('werror')
  SPHINX_ARGS += [ '-W' ]
endif

if build_docs
  configure_file(output: 'index.html',
                 input: files('index.html.in'),
                 configuration: {'VERSION': meson.project_version()},
                 install_dir: qemu_docdir)
  manuals = [ 'devel', 'interop', 'tools', 'specs', 'system', 'user' ]
  man_pages = {
    'interop' : {
        'qemu-ga.8': (have_tools ? 'man8' : ''),
        'qemu-ga-ref.7': 'man7',
    },
    'tools': {
        'qemu-img.1': (have_tools ? 'man1' : ''),
        'qemu-nbd.8': (have_tools ? 'man8' : ''),
        'qemu-trace-stap.1': (config_host.has_key('CONFIG_TRACE_SYSTEMTAP') ? 'man1' : ''),
        'virtfs-proxy-helper.1': (have_virtfs_proxy_helper ? 'man1' : ''),
        'virtiofsd.1': (have_virtiofsd ? 'man1' : ''),
    },
    'system': {
        'qemu.1': 'man1',
        'qemu-block-drivers.7': 'man7',
        'qemu-cpu-models.7': 'man7'
    },
  }

  sphinxdocs = []
  sphinxmans = []
  foreach manual : manuals
    private_dir = meson.current_build_dir() / (manual + '.p')
    output_dir = meson.current_build_dir() / manual
    input_dir = meson.current_source_dir() / manual

    this_manual = custom_target(manual + ' manual',
                build_by_default: build_docs,
                output: [manual + '.stamp'],
                input: [files('conf.py'), files(manual / 'conf.py')],
                depfile: manual + '.d',
                command: [SPHINX_ARGS, '-Ddepfile=@DEPFILE@',
                          '-Ddepfile_stamp=@OUTPUT0@',
                          '-b', 'html', '-d', private_dir,
                          input_dir, output_dir])
    sphinxdocs += this_manual
    if build_docs and manual != 'devel'
      install_subdir(output_dir, install_dir: qemu_docdir)
    endif

    these_man_pages = []
    install_dirs = []
    foreach page, section : man_pages.get(manual, {})
      these_man_pages += page
      install_dirs += section == '' ? false : get_option('mandir') / section
    endforeach
    if these_man_pages.length() > 0
      sphinxmans += custom_target(manual + ' man pages',
                         build_by_default: build_docs,
                         output: these_man_pages,
                         input: this_manual,
                         install: build_docs,
                         install_dir: install_dirs,
                         command: [SPHINX_ARGS, '-b', 'man', '-d', private_dir,
                                   input_dir, meson.current_build_dir()])
    endif
  endforeach
  alias_target('sphinxdocs', sphinxdocs)
  alias_target('man', sphinxmans)
endif
