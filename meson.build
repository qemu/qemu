project('qemu', ['c'], meson_version: '>=0.55.0',
        default_options: ['warning_level=1', 'c_std=gnu99', 'cpp_std=gnu++11', 'b_lundef=false'],
        version: run_command('head', meson.source_root() / 'VERSION').stdout().strip())

not_found = dependency('', required: false)
keyval = import('unstable-keyval')
config_host = keyval.load(meson.current_build_dir() / 'config-host.mak')

add_project_arguments(config_host['QEMU_CFLAGS'].split(),
                      native: false, language: ['c', 'objc'])
add_project_arguments(config_host['QEMU_CXXFLAGS'].split(),
                      native: false, language: 'cpp')
add_project_link_arguments(config_host['QEMU_LDFLAGS'].split(),
                           native: false, language: ['c', 'cpp', 'objc'])
add_project_arguments(config_host['QEMU_INCLUDES'].split(),
                      language: ['c', 'cpp', 'objc'])

python = import('python').find_installation()

link_language = meson.get_external_property('link_language', 'cpp')
if link_language == 'cpp'
  add_languages('cpp', required: true, native: false)
endif
if host_machine.system() == 'darwin'
  add_languages('objc', required: false, native: false)
endif

configure_file(input: files('scripts/ninjatool.py'),
               output: 'ninjatool',
               configuration: config_host)

supported_oses = ['windows', 'freebsd', 'netbsd', 'openbsd', 'darwin', 'sunos', 'linux']
supported_cpus = ['ppc', 'ppc64', 's390x', 'sparc64', 'riscv32', 'riscv64', 'x86', 'x86_64',
  'arm', 'aarch64', 'mips', 'mips64', 'sparc', 'sparc64']

cpu = host_machine.cpu_family()
targetos = host_machine.system()

summary_info = {}
summary_info += {'Install prefix':    config_host['prefix']}
summary_info += {'BIOS directory':    config_host['qemu_datadir']}
summary_info += {'firmware path':     config_host['qemu_firmwarepath']}
summary_info += {'binary directory':  config_host['bindir']}
summary_info += {'library directory': config_host['libdir']}
summary_info += {'module directory':  config_host['qemu_moddir']}
summary_info += {'libexec directory': config_host['libexecdir']}
summary_info += {'include directory': config_host['includedir']}
summary_info += {'config directory':  config_host['sysconfdir']}
if targetos != 'windows'
  summary_info += {'local state directory': config_host['qemu_localstatedir']}
  summary_info += {'Manual directory':      config_host['mandir']}
else
  summary_info += {'local state directory': 'queried at runtime'}
endif
summary_info += {'Build directory':   meson.current_build_dir()}
summary_info += {'Source path':       meson.current_source_dir()}
summary_info += {'GIT binary':        config_host['GIT']}
summary_info += {'GIT submodules':    config_host['GIT_SUBMODULES']}
summary_info += {'C compiler':        meson.get_compiler('c').cmd_array()[0]}
summary_info += {'Host C compiler':   meson.get_compiler('c', native: true).cmd_array()[0]}
if link_language == 'cpp'
  summary_info += {'C++ compiler':      meson.get_compiler('cpp').cmd_array()[0]}
else
  summary_info += {'C++ compiler':      false}
endif
if targetos == 'darwin'
  summary_info += {'Objective-C compiler': meson.get_compiler('objc').cmd_array()[0]}
endif
summary_info += {'ARFLAGS':           config_host['ARFLAGS']}
summary_info += {'CFLAGS':            config_host['CFLAGS']}
summary_info += {'QEMU_CFLAGS':       config_host['QEMU_CFLAGS']}
summary_info += {'QEMU_LDFLAGS':      config_host['QEMU_LDFLAGS']}
summary_info += {'make':              config_host['MAKE']}
summary_info += {'install':           config_host['INSTALL']}
summary_info += {'python':            '@0@ (version: @1@)'.format(python.full_path(), python.language_version())}
summary_info += {'sphinx-build':      config_host['SPHINX_BUILD']}
summary_info += {'genisoimage':       config_host['GENISOIMAGE']}
# TODO: add back version
summary_info += {'slirp support':     config_host.has_key('CONFIG_SLIRP')}
if config_host.has_key('CONFIG_SLIRP')
  summary_info += {'smbd':            config_host['CONFIG_SMBD_COMMAND']}
endif
summary_info += {'module support':    config_host.has_key('CONFIG_MODULES')}
if config_host.has_key('CONFIG_MODULES')
  summary_info += {'alternative module path': config_host.has_key('CONFIG_MODULE_UPGRADES')}
endif
summary_info += {'host CPU':          cpu}
summary_info += {'host endianness':   build_machine.endian()}
summary_info += {'target list':       config_host['TARGET_DIRS']}
summary_info += {'gprof enabled':     config_host.has_key('CONFIG_GPROF')}
summary_info += {'sparse enabled':    meson.get_compiler('c').cmd_array().contains('cgcc')}
summary_info += {'strip binaries':    get_option('strip')}
summary_info += {'profiler':          config_host.has_key('CONFIG_PROFILER')}
summary_info += {'static build':      config_host.has_key('CONFIG_TOOLS')}
if targetos == 'darwin'
  summary_info += {'Cocoa support': config_host.has_key('CONFIG_COCOA')}
endif
# TODO: add back version
summary_info += {'SDL support':       config_host.has_key('CONFIG_SDL')}
summary_info += {'SDL image support': config_host.has_key('CONFIG_SDL_IMAGE')}
# TODO: add back version
summary_info += {'GTK support':       config_host.has_key('CONFIG_GTK')}
summary_info += {'GTK GL support':    config_host.has_key('CONFIG_GTK_GL')}
# TODO: add back version
summary_info += {'VTE support':       config_host.has_key('CONFIG_VTE')}
summary_info += {'TLS priority':      config_host['CONFIG_TLS_PRIORITY']}
summary_info += {'GNUTLS support':    config_host.has_key('CONFIG_GNUTLS')}
# TODO: add back version
summary_info += {'libgcrypt':         config_host.has_key('CONFIG_GCRYPT')}
if config_host.has_key('CONFIG_GCRYPT')
   summary_info += {'  hmac':            config_host.has_key('CONFIG_GCRYPT_HMAC')}
   summary_info += {'  XTS':             not config_host.has_key('CONFIG_QEMU_PRIVATE_XTS')}
endif
# TODO: add back version
summary_info += {'nettle':            config_host.has_key('CONFIG_NETTLE')}
if config_host.has_key('CONFIG_NETTLE')
   summary_info += {'  XTS':             not config_host.has_key('CONFIG_QEMU_PRIVATE_XTS')}
endif
summary_info += {'libtasn1':          config_host.has_key('CONFIG_TASN1')}
summary_info += {'PAM':               config_host.has_key('CONFIG_AUTH_PAM')}
summary_info += {'iconv support':     config_host.has_key('CONFIG_ICONV')}
summary_info += {'curses support':    config_host.has_key('CONFIG_CURSES')}
# TODO: add back version
summary_info += {'virgl support':     config_host.has_key('CONFIG_VIRGL')}
summary_info += {'curl support':      config_host.has_key('CONFIG_CURL')}
summary_info += {'mingw32 support':   targetos == 'windows'}
summary_info += {'Audio drivers':     config_host['CONFIG_AUDIO_DRIVERS']}
summary_info += {'Block whitelist (rw)': config_host['CONFIG_BDRV_RW_WHITELIST']}
summary_info += {'Block whitelist (ro)': config_host['CONFIG_BDRV_RO_WHITELIST']}
summary_info += {'VirtFS support':    config_host.has_key('CONFIG_VIRTFS')}
summary_info += {'Multipath support': config_host.has_key('CONFIG_MPATH')}
summary_info += {'VNC support':       config_host.has_key('CONFIG_VNC')}
if config_host.has_key('CONFIG_VNC')
  summary_info += {'VNC SASL support':  config_host.has_key('CONFIG_VNC_SASL')}
  summary_info += {'VNC JPEG support':  config_host.has_key('CONFIG_VNC_JPEG')}
  summary_info += {'VNC PNG support':   config_host.has_key('CONFIG_VNC_PNG')}
endif
summary_info += {'xen support':       config_host.has_key('CONFIG_XEN_BACKEND')}
if config_host.has_key('CONFIG_XEN_BACKEND')
  summary_info += {'xen ctrl version':  config_host['CONFIG_XEN_CTRL_INTERFACE_VERSION']}
endif
summary_info += {'brlapi support':    config_host.has_key('CONFIG_BRLAPI')}
summary_info += {'Documentation':     config_host.has_key('BUILD_DOCS')}
summary_info += {'PIE':               get_option('b_pie')}
summary_info += {'vde support':       config_host.has_key('CONFIG_VDE')}
summary_info += {'netmap support':    config_host.has_key('CONFIG_NETMAP')}
summary_info += {'Linux AIO support': config_host.has_key('CONFIG_LINUX_AIO')}
summary_info += {'Linux io_uring support': config_host.has_key('CONFIG_LINUX_IO_URING')}
summary_info += {'ATTR/XATTR support': config_host.has_key('CONFIG_ATTR')}
summary_info += {'Install blobs':     config_host.has_key('INSTALL_BLOBS')}
# TODO: add back KVM/HAX/HVF/WHPX/TCG
#summary_info += {'KVM support':       have_kvm'}
#summary_info += {'HAX support':       have_hax'}
#summary_info += {'HVF support':       have_hvf'}
#summary_info += {'WHPX support':      have_whpx'}
#summary_info += {'TCG support':       have_tcg'}
#if get_option('tcg')
#  summary_info += {'TCG debug enabled': config_host.has_key('CONFIG_DEBUG_TCG')}
#  summary_info += {'TCG interpreter':   config_host.has_key('CONFIG_TCG_INTERPRETER')}
#endif
summary_info += {'malloc trim support': config_host.has_key('CONFIG_MALLOC_TRIM')}
summary_info += {'RDMA support':      config_host.has_key('CONFIG_RDMA')}
summary_info += {'PVRDMA support':    config_host.has_key('CONFIG_PVRDMA')}
summary_info += {'fdt support':       config_host.has_key('CONFIG_FDT')}
summary_info += {'membarrier':        config_host.has_key('CONFIG_MEMBARRIER')}
summary_info += {'preadv support':    config_host.has_key('CONFIG_PREADV')}
summary_info += {'fdatasync':         config_host.has_key('CONFIG_FDATASYNC')}
summary_info += {'madvise':           config_host.has_key('CONFIG_MADVISE')}
summary_info += {'posix_madvise':     config_host.has_key('CONFIG_POSIX_MADVISE')}
summary_info += {'posix_memalign':    config_host.has_key('CONFIG_POSIX_MEMALIGN')}
summary_info += {'libcap-ng support': config_host.has_key('CONFIG_LIBCAP_NG')}
summary_info += {'vhost-net support': config_host.has_key('CONFIG_VHOST_NET')}
summary_info += {'vhost-crypto support': config_host.has_key('CONFIG_VHOST_CRYPTO')}
summary_info += {'vhost-scsi support': config_host.has_key('CONFIG_VHOST_SCSI')}
summary_info += {'vhost-vsock support': config_host.has_key('CONFIG_VHOST_VSOCK')}
summary_info += {'vhost-user support': config_host.has_key('CONFIG_VHOST_KERNEL')}
summary_info += {'vhost-user-fs support': config_host.has_key('CONFIG_VHOST_USER_FS')}
summary_info += {'vhost-vdpa support': config_host.has_key('CONFIG_VHOST_VDPA')}
summary_info += {'Trace backends':    config_host['TRACE_BACKENDS']}
if config_host['TRACE_BACKENDS'].split().contains('simple')
  summary_info += {'Trace output file': config_host['CONFIG_TRACE_FILE'] + '-<pid>'}
endif
# TODO: add back protocol and server version
summary_info += {'spice support':     config_host.has_key('CONFIG_SPICE')}
summary_info += {'rbd support':       config_host.has_key('CONFIG_RBD')}
summary_info += {'xfsctl support':    config_host.has_key('CONFIG_XFS')}
summary_info += {'smartcard support': config_host.has_key('CONFIG_SMARTCARD')}
summary_info += {'libusb':            config_host.has_key('CONFIG_USB_LIBUSB')}
summary_info += {'usb net redir':     config_host.has_key('CONFIG_USB_REDIR')}
summary_info += {'OpenGL support':    config_host.has_key('CONFIG_OPENGL')}
summary_info += {'OpenGL dmabufs':    config_host.has_key('CONFIG_OPENGL_DMABUF')}
summary_info += {'libiscsi support':  config_host.has_key('CONFIG_LIBISCSI')}
summary_info += {'libnfs support':    config_host.has_key('CONFIG_LIBNFS')}
summary_info += {'build guest agent': config_host.has_key('CONFIG_GUEST_AGENT')}
if targetos == 'windows'
  if 'WIN_SDK' in config_host
    summary_info += {'Windows SDK':       config_host['WIN_SDK']}
  endif
  summary_info += {'QGA VSS support':   config_host.has_key('CONFIG_QGA_VSS')}
  summary_info += {'QGA w32 disk info': config_host.has_key('CONFIG_QGA_NTDDSCSI')}
  summary_info += {'QGA MSI support':   config_host.has_key('CONFIG_QGA_MSI_ENABLED')}
endif
summary_info += {'seccomp support':   config_host.has_key('CONFIG_SECCOMP')}
summary_info += {'coroutine backend': config_host['CONFIG_COROUTINE_BACKEND']}
summary_info += {'coroutine pool':    config_host['CONFIG_COROUTINE_POOL'] == '1'}
summary_info += {'debug stack usage': config_host.has_key('CONFIG_DEBUG_STACK_USAGE')}
summary_info += {'mutex debugging':   config_host.has_key('CONFIG_DEBUG_MUTEX')}
summary_info += {'crypto afalg':      config_host.has_key('CONFIG_AF_ALG')}
summary_info += {'GlusterFS support': config_host.has_key('CONFIG_GLUSTERFS')}
summary_info += {'gcov':              config_host.has_key('CONFIG_GCOV')}
summary_info += {'TPM support':       config_host.has_key('CONFIG_TPM')}
summary_info += {'libssh support':    config_host.has_key('CONFIG_LIBSSH')}
summary_info += {'QOM debugging':     config_host.has_key('CONFIG_QOM_CAST_DEBUG')}
summary_info += {'Live block migration': config_host.has_key('CONFIG_LIVE_BLOCK_MIGRATION')}
summary_info += {'lzo support':       config_host.has_key('CONFIG_LZO')}
summary_info += {'snappy support':    config_host.has_key('CONFIG_SNAPPY')}
summary_info += {'bzip2 support':     config_host.has_key('CONFIG_BZIP2')}
summary_info += {'lzfse support':     config_host.has_key('CONFIG_LZFSE')}
summary_info += {'zstd support':      config_host.has_key('CONFIG_ZSTD')}
summary_info += {'NUMA host support': config_host.has_key('CONFIG_NUMA')}
summary_info += {'libxml2':           config_host.has_key('CONFIG_LIBXML2')}
summary_info += {'tcmalloc support':  config_host.has_key('CONFIG_TCMALLOC')}
summary_info += {'jemalloc support':  config_host.has_key('CONFIG_JEMALLOC')}
summary_info += {'avx2 optimization': config_host.has_key('CONFIG_AVX2_OPT')}
summary_info += {'avx512f optimization': config_host.has_key('CONFIG_AVX512F_OPT')}
summary_info += {'replication support': config_host.has_key('CONFIG_REPLICATION')}
summary_info += {'bochs support':     config_host.has_key('CONFIG_BOCHS')}
summary_info += {'cloop support':     config_host.has_key('CONFIG_CLOOP')}
summary_info += {'dmg support':       config_host.has_key('CONFIG_DMG')}
summary_info += {'qcow v1 support':   config_host.has_key('CONFIG_QCOW1')}
summary_info += {'vdi support':       config_host.has_key('CONFIG_VDI')}
summary_info += {'vvfat support':     config_host.has_key('CONFIG_VVFAT')}
summary_info += {'qed support':       config_host.has_key('CONFIG_QED')}
summary_info += {'parallels support': config_host.has_key('CONFIG_PARALLELS')}
summary_info += {'sheepdog support':  config_host.has_key('CONFIG_SHEEPDOG')}
summary_info += {'capstone':          config_host.has_key('CONFIG_CAPSTONE')}
summary_info += {'libpmem support':   config_host.has_key('CONFIG_LIBPMEM')}
summary_info += {'libdaxctl support': config_host.has_key('CONFIG_LIBDAXCTL')}
summary_info += {'libudev':           config_host.has_key('CONFIG_LIBUDEV')}
summary_info += {'default devices':   config_host['CONFIG_MINIKCONF_MODE'] == '--defconfig'}
summary_info += {'plugin support':    config_host.has_key('CONFIG_PLUGIN')}
summary_info += {'fuzzing support':   config_host.has_key('CONFIG_FUZZ')}
if config_host.has_key('HAVE_GDB_BIN')
  summary_info += {'gdb':             config_host['HAVE_GDB_BIN']}
endif
summary_info += {'thread sanitizer':  config_host.has_key('CONFIG_TSAN')}
summary_info += {'rng-none':          config_host.has_key('CONFIG_RNG_NONE')}
summary_info += {'Linux keyring':     config_host.has_key('CONFIG_SECRET_KEYRING')}
summary(summary_info, bool_yn: true)

if not supported_cpus.contains(cpu)
  message()
  warning('SUPPORT FOR THIS HOST CPU WILL GO AWAY IN FUTURE RELEASES!')
  message()
  message('CPU host architecture ' + cpu + ' support is not currently maintained.')
  message('The QEMU project intends to remove support for this host CPU in')
  message('a future release if nobody volunteers to maintain it and to')
  message('provide a build host for our continuous integration setup.')
  message('configure has succeeded and you can continue to build, but')
  message('if you care about QEMU on this platform you should contact')
  message('us upstream at qemu-devel@nongnu.org.')
endif

if not supported_oses.contains(targetos)
  message()
  warning('WARNING: SUPPORT FOR THIS HOST OS WILL GO AWAY IN FUTURE RELEASES!')
  message()
  message('Host OS ' + targetos + 'support is not currently maintained.')
  message('The QEMU project intends to remove support for this host OS in')
  message('a future release if nobody volunteers to maintain it and to')
  message('provide a build host for our continuous integration setup.')
  message('configure has succeeded and you can continue to build, but')
  message('if you care about QEMU on this platform you should contact')
  message('us upstream at qemu-devel@nongnu.org.')
endif
