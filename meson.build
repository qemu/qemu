project('qemu', ['c'], meson_version: '>=0.58.2',
        default_options: ['warning_level=1', 'c_std=gnu11', 'cpp_std=gnu++11', 'b_colorout=auto',
                          'b_staticpic=false'],
        version: files('VERSION'))

not_found = dependency('', required: false)
keyval = import('keyval')
ss = import('sourceset')
fs = import('fs')

sh = find_program('sh')
cc = meson.get_compiler('c')
config_host = keyval.load(meson.current_build_dir() / 'config-host.mak')
enable_modules = 'CONFIG_MODULES' in config_host
enable_static = 'CONFIG_STATIC' in config_host

# Allow both shared and static libraries unless --enable-static
static_kwargs = enable_static ? {'static': true} : {}

# Temporary directory used for files created while
# configure runs. Since it is in the build directory
# we can safely blow away any previous version of it
# (and we need not jump through hoops to try to delete
# it when configure exits.)
tmpdir = meson.current_build_dir() / 'meson-private/temp'

if get_option('qemu_suffix').startswith('/')
  error('qemu_suffix cannot start with a /')
endif

qemu_confdir = get_option('sysconfdir') / get_option('qemu_suffix')
qemu_datadir = get_option('datadir') / get_option('qemu_suffix')
qemu_docdir = get_option('docdir') / get_option('qemu_suffix')
qemu_moddir = get_option('libdir') / get_option('qemu_suffix')

qemu_desktopdir = get_option('datadir') / 'applications'
qemu_icondir = get_option('datadir') / 'icons'

config_host_data = configuration_data()
genh = []

target_dirs = config_host['TARGET_DIRS'].split()
have_user = false
have_system = false
foreach target : target_dirs
  have_user = have_user or target.endswith('-user')
  have_system = have_system or target.endswith('-softmmu')
endforeach
have_tools = 'CONFIG_TOOLS' in config_host
have_block = have_system or have_tools

python = import('python').find_installation()

supported_oses = ['windows', 'freebsd', 'netbsd', 'openbsd', 'darwin', 'sunos', 'linux']
supported_cpus = ['ppc', 'ppc64', 's390x', 'riscv32', 'riscv64', 'x86', 'x86_64',
  'arm', 'aarch64', 'mips', 'mips64', 'sparc', 'sparc64']

cpu = host_machine.cpu_family()
targetos = host_machine.system()

if cpu in ['x86', 'x86_64']
  kvm_targets = ['i386-softmmu', 'x86_64-softmmu']
elif cpu == 'aarch64'
  kvm_targets = ['aarch64-softmmu']
elif cpu == 's390x'
  kvm_targets = ['s390x-softmmu']
elif cpu in ['ppc', 'ppc64']
  kvm_targets = ['ppc-softmmu', 'ppc64-softmmu']
elif cpu in ['mips', 'mips64']
  kvm_targets = ['mips-softmmu', 'mipsel-softmmu', 'mips64-softmmu', 'mips64el-softmmu']
else
  kvm_targets = []
endif

accelerator_targets = { 'CONFIG_KVM': kvm_targets }

if cpu in ['aarch64']
  accelerator_targets += {
    'CONFIG_HVF': ['aarch64-softmmu']
  }
endif

if cpu in ['x86', 'x86_64', 'arm', 'aarch64']
  # i386 emulator provides xenpv machine type for multiple architectures
  accelerator_targets += {
    'CONFIG_XEN': ['i386-softmmu', 'x86_64-softmmu'],
  }
endif
if cpu in ['x86', 'x86_64']
  accelerator_targets += {
    'CONFIG_HAX': ['i386-softmmu', 'x86_64-softmmu'],
    'CONFIG_HVF': ['x86_64-softmmu'],
    'CONFIG_NVMM': ['i386-softmmu', 'x86_64-softmmu'],
    'CONFIG_WHPX': ['i386-softmmu', 'x86_64-softmmu'],
  }
endif

modular_tcg = []
# Darwin does not support references to thread-local variables in modules
if targetos != 'darwin'
  modular_tcg = ['i386-softmmu', 'x86_64-softmmu']
endif

edk2_targets = [ 'arm-softmmu', 'aarch64-softmmu', 'i386-softmmu', 'x86_64-softmmu' ]
unpack_edk2_blobs = false
foreach target : edk2_targets
  if target in target_dirs
    bzip2 = find_program('bzip2', required: get_option('install_blobs'))
    unpack_edk2_blobs = bzip2.found()
    break
  endif
endforeach

##################
# Compiler flags #
##################

# Specify linker-script with add_project_link_arguments so that it is not placed
# within a linker --start-group/--end-group pair
if 'CONFIG_FUZZ' in config_host
   add_project_link_arguments(['-Wl,-T,',
                               (meson.current_source_dir() / 'tests/qtest/fuzz/fork_fuzz.ld')],
                              native: false, language: ['c', 'cpp', 'objc'])
endif

add_global_arguments(config_host['QEMU_CFLAGS'].split(),
                     native: false, language: ['c', 'objc'])
add_global_arguments(config_host['QEMU_CXXFLAGS'].split(),
                     native: false, language: 'cpp')
add_global_link_arguments(config_host['QEMU_LDFLAGS'].split(),
                          native: false, language: ['c', 'cpp', 'objc'])

if targetos == 'linux'
  add_project_arguments('-isystem', meson.current_source_dir() / 'linux-headers',
                        '-isystem', 'linux-headers',
                        language: ['c', 'cpp'])
endif

add_project_arguments('-iquote', '.',
                      '-iquote', meson.current_source_dir(),
                      '-iquote', meson.current_source_dir() / 'include',
                      '-iquote', meson.current_source_dir() / 'disas/libvixl',
                      language: ['c', 'cpp', 'objc'])

link_language = meson.get_external_property('link_language', 'cpp')
if link_language == 'cpp'
  add_languages('cpp', required: true, native: false)
endif
if host_machine.system() == 'darwin'
  add_languages('objc', required: false, native: false)
endif

sparse = find_program('cgcc', required: get_option('sparse'))
if sparse.found()
  run_target('sparse',
             command: [find_program('scripts/check_sparse.py'),
                       'compile_commands.json', sparse.full_path(), '-Wbitwise',
                       '-Wno-transparent-union', '-Wno-old-initializer',
                       '-Wno-non-pointer-null'])
endif

###########################################
# Target-specific checks and dependencies #
###########################################

if targetos != 'linux' and get_option('mpath').enabled()
  error('Multipath is supported only on Linux')
endif

if targetos != 'linux' and get_option('multiprocess').enabled()
  error('Multiprocess QEMU is supported only on Linux')
endif
multiprocess_allowed = targetos == 'linux' and not get_option('multiprocess').disabled()

libm = cc.find_library('m', required: false)
threads = dependency('threads')
util = cc.find_library('util', required: false)
winmm = []
socket = []
version_res = []
coref = []
iokit = []
emulator_link_args = []
nvmm =not_found
hvf = not_found
if targetos == 'windows'
  socket = cc.find_library('ws2_32')
  winmm = cc.find_library('winmm')

  win = import('windows')
  version_res = win.compile_resources('version.rc',
                                      depend_files: files('pc-bios/qemu-nsis.ico'),
                                      include_directories: include_directories('.'))
elif targetos == 'darwin'
  coref = dependency('appleframeworks', modules: 'CoreFoundation')
  iokit = dependency('appleframeworks', modules: 'IOKit', required: false)
elif targetos == 'sunos'
  socket = [cc.find_library('socket'),
            cc.find_library('nsl'),
            cc.find_library('resolv')]
elif targetos == 'haiku'
  socket = [cc.find_library('posix_error_mapper'),
            cc.find_library('network'),
            cc.find_library('bsd')]
elif targetos == 'openbsd'
  if not get_option('tcg').disabled() and target_dirs.length() > 0
    # Disable OpenBSD W^X if available
    emulator_link_args = cc.get_supported_link_arguments('-Wl,-z,wxneeded')
  endif
endif

accelerators = []
if not get_option('kvm').disabled() and targetos == 'linux'
  accelerators += 'CONFIG_KVM'
endif
if not get_option('xen').disabled() and 'CONFIG_XEN_BACKEND' in config_host
  accelerators += 'CONFIG_XEN'
  have_xen_pci_passthrough = not get_option('xen_pci_passthrough').disabled() and targetos == 'linux'
else
  have_xen_pci_passthrough = false
endif
if not get_option('whpx').disabled() and targetos == 'windows'
  if get_option('whpx').enabled() and host_machine.cpu() != 'x86_64'
    error('WHPX requires 64-bit host')
  elif cc.has_header('WinHvPlatform.h', required: get_option('whpx')) and \
       cc.has_header('WinHvEmulation.h', required: get_option('whpx'))
    accelerators += 'CONFIG_WHPX'
  endif
endif
if not get_option('hvf').disabled()
  hvf = dependency('appleframeworks', modules: 'Hypervisor',
                   required: get_option('hvf'))
  if hvf.found()
    accelerators += 'CONFIG_HVF'
  endif
endif
if not get_option('hax').disabled()
  if get_option('hax').enabled() or targetos in ['windows', 'darwin', 'netbsd']
    accelerators += 'CONFIG_HAX'
  endif
endif
if targetos == 'netbsd'
  if cc.has_header_symbol('nvmm.h', 'nvmm_cpu_stop', required: get_option('nvmm'))
    nvmm = cc.find_library('nvmm', required: get_option('nvmm'))
  endif
  if nvmm.found()
    accelerators += 'CONFIG_NVMM'
  endif
endif

tcg_arch = config_host['ARCH']
if not get_option('tcg').disabled()
  if cpu not in supported_cpus
    if get_option('tcg_interpreter')
      warning('Unsupported CPU @0@, will use TCG with TCI (experimental and slow)'.format(cpu))
    else
      error('Unsupported CPU @0@, try --enable-tcg-interpreter'.format(cpu))
    endif
  elif get_option('tcg_interpreter')
    warning('Use of the TCG interpretor is not recommended on this host')
    warning('architecture. There is a native TCG execution backend available')
    warning('which provides substantially better performance and reliability.')
    warning('It is strongly recommended to remove the --enable-tcg-interpreter')
    warning('configuration option on this architecture to use the native')
    warning('backend.')
  endif
  if get_option('tcg_interpreter')
    tcg_arch = 'tci'
  elif config_host['ARCH'] == 'sparc64'
    tcg_arch = 'sparc'
  elif config_host['ARCH'] in ['x86_64', 'x32']
    tcg_arch = 'i386'
  elif config_host['ARCH'] == 'ppc64'
    tcg_arch = 'ppc'
  elif config_host['ARCH'] in ['riscv32', 'riscv64']
    tcg_arch = 'riscv'
  endif
  add_project_arguments('-iquote', meson.current_source_dir() / 'tcg' / tcg_arch,
                        language: ['c', 'cpp', 'objc'])

  accelerators += 'CONFIG_TCG'
  config_host += { 'CONFIG_TCG': 'y' }
endif

if 'CONFIG_KVM' not in accelerators and get_option('kvm').enabled()
  error('KVM not available on this platform')
endif
if 'CONFIG_HVF' not in accelerators and get_option('hvf').enabled()
  error('HVF not available on this platform')
endif
if 'CONFIG_NVMM' not in accelerators and get_option('nvmm').enabled()
  error('NVMM not available on this platform')
endif
if 'CONFIG_WHPX' not in accelerators and get_option('whpx').enabled()
  error('WHPX not available on this platform')
endif
if not have_xen_pci_passthrough and get_option('xen_pci_passthrough').enabled()
  if 'CONFIG_XEN' in accelerators
    error('Xen PCI passthrough not available on this platform')
  else
    error('Xen PCI passthrough requested but Xen not enabled')
  endif
endif

################
# Dependencies #
################

# The path to glib.h is added to all compilation commands.  This was
# grandfathered in from the QEMU Makefiles.
add_project_arguments(config_host['GLIB_CFLAGS'].split(),
                      native: false, language: ['c', 'cpp', 'objc'])
glib = declare_dependency(compile_args: config_host['GLIB_CFLAGS'].split(),
                          link_args: config_host['GLIB_LIBS'].split())
# override glib dep with the configure results (for subprojects)
meson.override_dependency('glib-2.0', glib)

gio = not_found
if 'CONFIG_GIO' in config_host
  gio = declare_dependency(compile_args: config_host['GIO_CFLAGS'].split(),
                           link_args: config_host['GIO_LIBS'].split())
endif
lttng = not_found
if 'CONFIG_TRACE_UST' in config_host
  lttng = declare_dependency(link_args: config_host['LTTNG_UST_LIBS'].split())
endif
pixman = not_found
if have_system or have_tools
  pixman = dependency('pixman-1', required: have_system, version:'>=0.21.8',
                      method: 'pkg-config', kwargs: static_kwargs)
endif
libaio = cc.find_library('aio', required: false)
zlib = dependency('zlib', required: true, kwargs: static_kwargs)

linux_io_uring = not_found
if not get_option('linux_io_uring').auto() or have_block
  linux_io_uring = dependency('liburing', required: get_option('linux_io_uring'),
                              method: 'pkg-config', kwargs: static_kwargs)
endif
libxml2 = not_found
if not get_option('libxml2').auto() or have_block
  libxml2 = dependency('libxml-2.0', required: get_option('libxml2'),
                       method: 'pkg-config', kwargs: static_kwargs)
endif
libnfs = not_found
if not get_option('libnfs').auto() or have_block
  libnfs = dependency('libnfs', version: '>=1.9.3',
                      required: get_option('libnfs'),
                      method: 'pkg-config', kwargs: static_kwargs)
endif

libattr_test = '''
  #include <stddef.h>
  #include <sys/types.h>
  #ifdef CONFIG_LIBATTR
  #include <attr/xattr.h>
  #else
  #include <sys/xattr.h>
  #endif
  int main(void) { getxattr(NULL, NULL, NULL, 0); setxattr(NULL, NULL, NULL, 0, 0); return 0; }'''

libattr = not_found
have_old_libattr = false
if not get_option('attr').disabled()
  if cc.links(libattr_test)
    libattr = declare_dependency()
  else
    libattr = cc.find_library('attr', has_headers: ['attr/xattr.h'],
                              required: get_option('attr'),
                              kwargs: static_kwargs)
    if libattr.found() and not \
      cc.links(libattr_test, dependencies: libattr, args: '-DCONFIG_LIBATTR')
      libattr = not_found
      if get_option('attr').enabled()
        error('could not link libattr')
      else
        warning('could not link libattr, disabling')
      endif
    else
      have_old_libattr = libattr.found()
    endif
  endif
endif

cocoa = dependency('appleframeworks', modules: 'Cocoa', required: get_option('cocoa'))
if cocoa.found() and get_option('sdl').enabled()
  error('Cocoa and SDL cannot be enabled at the same time')
endif
if cocoa.found() and get_option('gtk').enabled()
  error('Cocoa and GTK+ cannot be enabled at the same time')
endif

seccomp = not_found
if not get_option('seccomp').auto() or have_system or have_tools
  seccomp = dependency('libseccomp', version: '>=2.3.0',
                       required: get_option('seccomp'),
                       method: 'pkg-config', kwargs: static_kwargs)
endif

libcap_ng = not_found
if not get_option('cap_ng').auto() or have_system or have_tools
  libcap_ng = cc.find_library('cap-ng', has_headers: ['cap-ng.h'],
                              required: get_option('cap_ng'),
                              kwargs: static_kwargs)
endif
if libcap_ng.found() and not cc.links('''
   #include <cap-ng.h>
   int main(void)
   {
     capng_capability_to_name(CAPNG_EFFECTIVE);
     return 0;
   }''', dependencies: libcap_ng)
  libcap_ng = not_found
  if get_option('cap_ng').enabled()
    error('could not link libcap-ng')
  else
    warning('could not link libcap-ng, disabling')
  endif
endif

if get_option('xkbcommon').auto() and not have_system and not have_tools
  xkbcommon = not_found
else
  xkbcommon = dependency('xkbcommon', required: get_option('xkbcommon'),
                         method: 'pkg-config', kwargs: static_kwargs)
endif
vde = not_found
if config_host.has_key('CONFIG_VDE')
  vde = declare_dependency(link_args: config_host['VDE_LIBS'].split())
endif
pulse = not_found
if 'CONFIG_LIBPULSE' in config_host
  pulse = declare_dependency(compile_args: config_host['PULSE_CFLAGS'].split(),
                             link_args: config_host['PULSE_LIBS'].split())
endif
alsa = not_found
if 'CONFIG_ALSA' in config_host
  alsa = declare_dependency(compile_args: config_host['ALSA_CFLAGS'].split(),
                            link_args: config_host['ALSA_LIBS'].split())
endif
jack = not_found
if 'CONFIG_LIBJACK' in config_host
  jack = declare_dependency(link_args: config_host['JACK_LIBS'].split())
endif
spice = not_found
spice_headers = not_found
spice_protocol = not_found
if 'CONFIG_SPICE' in config_host
  spice = declare_dependency(compile_args: config_host['SPICE_CFLAGS'].split(),
                             link_args: config_host['SPICE_LIBS'].split())
  spice_headers = declare_dependency(compile_args: config_host['SPICE_CFLAGS'].split())
endif
if 'CONFIG_SPICE_PROTOCOL' in config_host
  spice_protocol = declare_dependency(compile_args: config_host['SPICE_PROTOCOL_CFLAGS'].split())
endif
rt = cc.find_library('rt', required: false)
libdl = not_found
if 'CONFIG_PLUGIN' in config_host
  libdl = cc.find_library('dl', required: false)
  if not cc.has_function('dlopen', dependencies: libdl)
    error('dlopen not found')
  endif
endif
libiscsi = not_found
if not get_option('libiscsi').auto() or have_block
  libiscsi = dependency('libiscsi', version: '>=1.9.0',
                         required: get_option('libiscsi'),
                         method: 'pkg-config', kwargs: static_kwargs)
endif
zstd = not_found
if not get_option('zstd').auto() or have_block
  zstd = dependency('libzstd', version: '>=1.4.0',
                    required: get_option('zstd'),
                    method: 'pkg-config', kwargs: static_kwargs)
endif
virgl = not_found
if not get_option('virglrenderer').auto() or have_system
  virgl = dependency('virglrenderer',
                     method: 'pkg-config',
                     required: get_option('virglrenderer'),
                     kwargs: static_kwargs)
endif
curl = not_found
if not get_option('curl').auto() or have_block
  curl = dependency('libcurl', version: '>=7.29.0',
                    method: 'pkg-config',
                    required: get_option('curl'),
                    kwargs: static_kwargs)
endif
libudev = not_found
if targetos == 'linux' and (have_system or have_tools)
  libudev = dependency('libudev',
                       method: 'pkg-config',
                       required: get_option('libudev'),
                       kwargs: static_kwargs)
endif

mpathlibs = [libudev]
mpathpersist = not_found
mpathpersist_new_api = false
if targetos == 'linux' and have_tools and not get_option('mpath').disabled()
  mpath_test_source_new = '''
    #include <libudev.h>
    #include <mpath_persist.h>
    unsigned mpath_mx_alloc_len = 1024;
    int logsink;
    static struct config *multipath_conf;
    extern struct udev *udev;
    extern struct config *get_multipath_config(void);
    extern void put_multipath_config(struct config *conf);
    struct udev *udev;
    struct config *get_multipath_config(void) { return multipath_conf; }
    void put_multipath_config(struct config *conf) { }
    int main(void) {
        udev = udev_new();
        multipath_conf = mpath_lib_init();
        return 0;
    }'''
  mpath_test_source_old = '''
      #include <libudev.h>
      #include <mpath_persist.h>
      unsigned mpath_mx_alloc_len = 1024;
      int logsink;
      int main(void) {
          struct udev *udev = udev_new();
          mpath_lib_init(udev);
          return 0;
      }'''
  libmpathpersist = cc.find_library('mpathpersist',
                                    required: get_option('mpath'),
                                    kwargs: static_kwargs)
  if libmpathpersist.found()
    mpathlibs += libmpathpersist
    if enable_static
      mpathlibs += cc.find_library('devmapper',
                                     required: get_option('mpath'),
                                     kwargs: static_kwargs)
    endif
    mpathlibs += cc.find_library('multipath',
                                 required: get_option('mpath'),
                                 kwargs: static_kwargs)
    foreach lib: mpathlibs
      if not lib.found()
        mpathlibs = []
        break
      endif
    endforeach
    if mpathlibs.length() == 0
      msg = 'Dependencies missing for libmpathpersist'
    elif cc.links(mpath_test_source_new, dependencies: mpathlibs)
      mpathpersist = declare_dependency(dependencies: mpathlibs)
      mpathpersist_new_api = true
    elif cc.links(mpath_test_source_old, dependencies: mpathlibs)
      mpathpersist = declare_dependency(dependencies: mpathlibs)
    else
      msg = 'Cannot detect libmpathpersist API'
    endif
    if not mpathpersist.found()
      if get_option('mpath').enabled()
        error(msg)
      else
        warning(msg + ', disabling')
      endif
    endif
  endif
endif

iconv = not_found
curses = not_found
if have_system and not get_option('curses').disabled()
  curses_test = '''
    #include <locale.h>
    #include <curses.h>
    #include <wchar.h>
    int main(void) {
      wchar_t wch = L'w';
      setlocale(LC_ALL, "");
      resize_term(0, 0);
      addwstr(L"wide chars\n");
      addnwstr(&wch, 1);
      add_wch(WACS_DEGREE);
      return 0;
    }'''

  curses_dep_list = targetos == 'windows' ? ['ncurses', 'ncursesw'] : ['ncursesw']
  foreach curses_dep : curses_dep_list
    if not curses.found()
      curses = dependency(curses_dep,
                          required: false,
                          method: 'pkg-config',
                          kwargs: static_kwargs)
    endif
  endforeach
  msg = get_option('curses').enabled() ? 'curses library not found' : ''
  curses_compile_args = ['-DNCURSES_WIDECHAR']
  if curses.found()
    if cc.links(curses_test, args: curses_compile_args, dependencies: [curses])
      curses = declare_dependency(compile_args: curses_compile_args, dependencies: [curses])
    else
      msg = 'curses package not usable'
      curses = not_found
    endif
  endif
  if not curses.found()
    has_curses_h = cc.has_header('curses.h', args: curses_compile_args)
    if targetos != 'windows' and not has_curses_h
      message('Trying with /usr/include/ncursesw')
      curses_compile_args += ['-I/usr/include/ncursesw']
      has_curses_h = cc.has_header('curses.h', args: curses_compile_args)
    endif
    if has_curses_h
      curses_libname_list = (targetos == 'windows' ? ['pdcurses'] : ['ncursesw', 'cursesw'])
      foreach curses_libname : curses_libname_list
        libcurses = cc.find_library(curses_libname,
                                    required: false,
                                    kwargs: static_kwargs)
        if libcurses.found()
          if cc.links(curses_test, args: curses_compile_args, dependencies: libcurses)
            curses = declare_dependency(compile_args: curses_compile_args,
                                        dependencies: [libcurses])
            break
          else
            msg = 'curses library not usable'
          endif
        endif
      endforeach
    endif
  endif
  if not get_option('iconv').disabled()
    foreach link_args : [ ['-liconv'], [] ]
      # Programs will be linked with glib and this will bring in libiconv on FreeBSD.
      # We need to use libiconv if available because mixing libiconv's headers with
      # the system libc does not work.
      # However, without adding glib to the dependencies -L/usr/local/lib will not be
      # included in the command line and libiconv will not be found.
      if cc.links('''
        #include <iconv.h>
        int main(void) {
          iconv_t conv = iconv_open("WCHAR_T", "UCS-2");
          return conv != (iconv_t) -1;
        }''', args: config_host['GLIB_CFLAGS'].split() + config_host['GLIB_LIBS'].split() + link_args)
        iconv = declare_dependency(link_args: link_args, dependencies: glib)
        break
      endif
    endforeach
  endif
  if curses.found() and not iconv.found()
    if get_option('iconv').enabled()
      error('iconv not available')
    endif
    msg = 'iconv required for curses UI but not available'
    curses = not_found
  endif
  if not curses.found() and msg != ''
    if get_option('curses').enabled()
      error(msg)
    else
      warning(msg + ', disabling')
    endif
  endif
endif

brlapi = not_found
if not get_option('brlapi').auto() or have_system
  brlapi = cc.find_library('brlapi', has_headers: ['brlapi.h'],
                         required: get_option('brlapi'),
                         kwargs: static_kwargs)
  if brlapi.found() and not cc.links('''
     #include <brlapi.h>
     #include <stddef.h>
     int main(void) { return brlapi__openConnection (NULL, NULL, NULL); }''', dependencies: brlapi)
    brlapi = not_found
    if get_option('brlapi').enabled()
      error('could not link brlapi')
    else
      warning('could not link brlapi, disabling')
    endif
  endif
endif

sdl = not_found
if not get_option('sdl').auto() or (have_system and not cocoa.found())
  sdl = dependency('sdl2', required: get_option('sdl'), kwargs: static_kwargs)
  sdl_image = not_found
endif
if sdl.found()
  # work around 2.0.8 bug
  sdl = declare_dependency(compile_args: '-Wno-undef',
                           dependencies: sdl)
  sdl_image = dependency('SDL2_image', required: get_option('sdl_image'),
                         method: 'pkg-config', kwargs: static_kwargs)
else
  if get_option('sdl_image').enabled()
    error('sdl-image required, but SDL was @0@'.format(
          get_option('sdl').disabled() ? 'disabled' : 'not found'))
  endif
  sdl_image = not_found
endif

rbd = not_found
if not get_option('rbd').auto() or have_block
  librados = cc.find_library('rados', required: get_option('rbd'),
                             kwargs: static_kwargs)
  librbd = cc.find_library('rbd', has_headers: ['rbd/librbd.h'],
                           required: get_option('rbd'),
                           kwargs: static_kwargs)
  if librados.found() and librbd.found()
    if cc.links('''
      #include <stdio.h>
      #include <rbd/librbd.h>
      int main(void) {
        rados_t cluster;
        rados_create(&cluster, NULL);
        #if LIBRBD_VERSION_CODE < LIBRBD_VERSION(1, 12, 0)
        #error
        #endif
        return 0;
      }''', dependencies: [librbd, librados])
      rbd = declare_dependency(dependencies: [librbd, librados])
    elif get_option('rbd').enabled()
      error('librbd >= 1.12.0 required')
    else
      warning('librbd >= 1.12.0 not found, disabling')
    endif
  endif
endif

glusterfs = not_found
glusterfs_ftruncate_has_stat = false
glusterfs_iocb_has_stat = false
if not get_option('glusterfs').auto() or have_block
  glusterfs = dependency('glusterfs-api', version: '>=3',
                         required: get_option('glusterfs'),
                         method: 'pkg-config', kwargs: static_kwargs)
  if glusterfs.found()
    glusterfs_ftruncate_has_stat = cc.links('''
      #include <glusterfs/api/glfs.h>

      int
      main(void)
      {
          /* new glfs_ftruncate() passes two additional args */
          return glfs_ftruncate(NULL, 0, NULL, NULL);
      }
    ''', dependencies: glusterfs)
    glusterfs_iocb_has_stat = cc.links('''
      #include <glusterfs/api/glfs.h>

      /* new glfs_io_cbk() passes two additional glfs_stat structs */
      static void
      glusterfs_iocb(glfs_fd_t *fd, ssize_t ret, struct glfs_stat *prestat, struct glfs_stat *poststat, void *data)
      {}

      int
      main(void)
      {
          glfs_io_cbk iocb = &glusterfs_iocb;
          iocb(NULL, 0 , NULL, NULL, NULL);
          return 0;
      }
    ''', dependencies: glusterfs)
  endif
endif
libssh = not_found
if 'CONFIG_LIBSSH' in config_host
  libssh = declare_dependency(compile_args: config_host['LIBSSH_CFLAGS'].split(),
                              link_args: config_host['LIBSSH_LIBS'].split())
endif
libbzip2 = not_found
if not get_option('bzip2').auto() or have_block
  libbzip2 = cc.find_library('bz2', has_headers: ['bzlib.h'],
                             required: get_option('bzip2'),
                             kwargs: static_kwargs)
  if libbzip2.found() and not cc.links('''
     #include <bzlib.h>
     int main(void) { BZ2_bzlibVersion(); return 0; }''', dependencies: libbzip2)
    libbzip2 = not_found
    if get_option('bzip2').enabled()
      error('could not link libbzip2')
    else
      warning('could not link libbzip2, disabling')
    endif
  endif
endif

liblzfse = not_found
if not get_option('lzfse').auto() or have_block
  liblzfse = cc.find_library('lzfse', has_headers: ['lzfse.h'],
                             required: get_option('lzfse'),
                             kwargs: static_kwargs)
endif
if liblzfse.found() and not cc.links('''
   #include <lzfse.h>
   int main(void) { lzfse_decode_scratch_size(); return 0; }''', dependencies: liblzfse)
  liblzfse = not_found
  if get_option('lzfse').enabled()
    error('could not link liblzfse')
  else
    warning('could not link liblzfse, disabling')
  endif
endif

oss = not_found
if 'CONFIG_AUDIO_OSS' in config_host
  oss = declare_dependency(link_args: config_host['OSS_LIBS'].split())
endif
dsound = not_found
if 'CONFIG_AUDIO_DSOUND' in config_host
  dsound = declare_dependency(link_args: config_host['DSOUND_LIBS'].split())
endif
coreaudio = not_found
if 'CONFIG_AUDIO_COREAUDIO' in config_host
  coreaudio = declare_dependency(link_args: config_host['COREAUDIO_LIBS'].split())
endif

opengl = not_found
if 'CONFIG_OPENGL' in config_host
  opengl = declare_dependency(compile_args: config_host['OPENGL_CFLAGS'].split(),
                              link_args: config_host['OPENGL_LIBS'].split())
endif
gbm = not_found
if (have_system or have_tools) and (virgl.found() or opengl.found())
  gbm = dependency('gbm', method: 'pkg-config', required: false,
                   kwargs: static_kwargs)
endif

gnutls = not_found
gnutls_crypto = not_found
if get_option('gnutls').enabled() or (get_option('gnutls').auto() and have_system)
  # For general TLS support our min gnutls matches
  # that implied by our platform support matrix
  #
  # For the crypto backends, we look for a newer
  # gnutls:
  #
  #   Version 3.6.8  is needed to get XTS
  #   Version 3.6.13 is needed to get PBKDF
  #   Version 3.6.14 is needed to get HW accelerated XTS
  #
  # If newer enough gnutls isn't available, we can
  # still use a different crypto backend to satisfy
  # the platform support requirements
  gnutls_crypto = dependency('gnutls', version: '>=3.6.14',
                             method: 'pkg-config',
                             required: false,
                             kwargs: static_kwargs)
  if gnutls_crypto.found()
    gnutls = gnutls_crypto
  else
    # Our min version if all we need is TLS
    gnutls = dependency('gnutls', version: '>=3.5.18',
                        method: 'pkg-config',
                        required: get_option('gnutls'),
                        kwargs: static_kwargs)
  endif
endif

# We prefer use of gnutls for crypto, unless the options
# explicitly asked for nettle or gcrypt.
#
# If gnutls isn't available for crypto, then we'll prefer
# gcrypt over nettle for performance reasons.
gcrypt = not_found
nettle = not_found
xts = 'none'

if get_option('nettle').enabled() and get_option('gcrypt').enabled()
  error('Only one of gcrypt & nettle can be enabled')
endif

# Explicit nettle/gcrypt request, so ignore gnutls for crypto
if get_option('nettle').enabled() or get_option('gcrypt').enabled()
  gnutls_crypto = not_found
endif

if not gnutls_crypto.found()
  if (not get_option('gcrypt').auto() or have_system) and not get_option('nettle').enabled()
    gcrypt = dependency('libgcrypt', version: '>=1.8',
                        method: 'config-tool',
                        required: get_option('gcrypt'),
                        kwargs: static_kwargs)
    # Debian has removed -lgpg-error from libgcrypt-config
    # as it "spreads unnecessary dependencies" which in
    # turn breaks static builds...
    if gcrypt.found() and enable_static
      gcrypt = declare_dependency(dependencies: [
        gcrypt,
        cc.find_library('gpg-error', required: true, kwargs: static_kwargs)])
    endif
  endif
  if (not get_option('nettle').auto() or have_system) and not gcrypt.found()
    nettle = dependency('nettle', version: '>=3.4',
                        method: 'pkg-config',
                        required: get_option('nettle'),
                        kwargs: static_kwargs)
    if nettle.found() and not cc.has_header('nettle/xts.h', dependencies: nettle)
      xts = 'private'
    endif
  endif
endif

gtk = not_found
gtkx11 = not_found
vte = not_found
if not get_option('gtk').auto() or (have_system and not cocoa.found())
  gtk = dependency('gtk+-3.0', version: '>=3.22.0',
                   method: 'pkg-config',
                   required: get_option('gtk'),
                   kwargs: static_kwargs)
  if gtk.found()
    gtkx11 = dependency('gtk+-x11-3.0', version: '>=3.22.0',
                        method: 'pkg-config',
                        required: false,
                        kwargs: static_kwargs)
    gtk = declare_dependency(dependencies: [gtk, gtkx11])

    if not get_option('vte').auto() or have_system
      vte = dependency('vte-2.91',
                       method: 'pkg-config',
                       required: get_option('vte'),
                       kwargs: static_kwargs)
    endif
  endif
endif

x11 = not_found
if gtkx11.found()
  x11 = dependency('x11', method: 'pkg-config', required: gtkx11.found(),
                   kwargs: static_kwargs)
endif
vnc = not_found
png = not_found
jpeg = not_found
sasl = not_found
if have_system and not get_option('vnc').disabled()
  vnc = declare_dependency() # dummy dependency
  png = dependency('libpng', required: get_option('vnc_png'),
                   method: 'pkg-config', kwargs: static_kwargs)
  jpeg = dependency('libjpeg', required: get_option('vnc_jpeg'),
                    method: 'pkg-config', kwargs: static_kwargs)
  sasl = cc.find_library('sasl2', has_headers: ['sasl/sasl.h'],
                         required: get_option('vnc_sasl'),
                         kwargs: static_kwargs)
  if sasl.found()
    sasl = declare_dependency(dependencies: sasl,
                              compile_args: '-DSTRUCT_IOVEC_DEFINED')
  endif
endif

pam = not_found
if not get_option('auth_pam').auto() or have_system
  pam = cc.find_library('pam', has_headers: ['security/pam_appl.h'],
                        required: get_option('auth_pam'),
                        kwargs: static_kwargs)
endif
if pam.found() and not cc.links('''
   #include <stddef.h>
   #include <security/pam_appl.h>
   int main(void) {
     const char *service_name = "qemu";
     const char *user = "frank";
     const struct pam_conv pam_conv = { 0 };
     pam_handle_t *pamh = NULL;
     pam_start(service_name, user, &pam_conv, &pamh);
     return 0;
   }''', dependencies: pam)
  pam = not_found
  if get_option('auth_pam').enabled()
    error('could not link libpam')
  else
    warning('could not link libpam, disabling')
  endif
endif

snappy = not_found
if not get_option('snappy').auto() or have_system
  snappy = cc.find_library('snappy', has_headers: ['snappy-c.h'],
                           required: get_option('snappy'),
                           kwargs: static_kwargs)
endif
if snappy.found() and not cc.links('''
   #include <snappy-c.h>
   int main(void) { snappy_max_compressed_length(4096); return 0; }''', dependencies: snappy)
  snappy = not_found
  if get_option('snappy').enabled()
    error('could not link libsnappy')
  else
    warning('could not link libsnappy, disabling')
  endif
endif

lzo = not_found
if not get_option('lzo').auto() or have_system
  lzo = cc.find_library('lzo2', has_headers: ['lzo/lzo1x.h'],
                        required: get_option('lzo'),
                        kwargs: static_kwargs)
endif
if lzo.found() and not cc.links('''
   #include <lzo/lzo1x.h>
   int main(void) { lzo_version(); return 0; }''', dependencies: lzo)
  lzo = not_found
  if get_option('lzo').enabled()
    error('could not link liblzo2')
  else
    warning('could not link liblzo2, disabling')
  endif
endif

rdma = not_found
if 'CONFIG_RDMA' in config_host
  rdma = declare_dependency(link_args: config_host['RDMA_LIBS'].split())
endif
numa = not_found
if 'CONFIG_NUMA' in config_host
  numa = declare_dependency(link_args: config_host['NUMA_LIBS'].split())
endif
xen = not_found
if 'CONFIG_XEN_BACKEND' in config_host
  xen = declare_dependency(compile_args: config_host['XEN_CFLAGS'].split(),
                           link_args: config_host['XEN_LIBS'].split())
endif
cacard = not_found
if not get_option('smartcard').auto() or have_system
  cacard = dependency('libcacard', required: get_option('smartcard'),
                      version: '>=2.5.1', method: 'pkg-config',
                      kwargs: static_kwargs)
endif
u2f = not_found
if have_system
  u2f = dependency('u2f-emu', required: get_option('u2f'),
                   method: 'pkg-config',
                   kwargs: static_kwargs)
endif
usbredir = not_found
if not get_option('usb_redir').auto() or have_system
  usbredir = dependency('libusbredirparser-0.5', required: get_option('usb_redir'),
                        version: '>=0.6', method: 'pkg-config',
                        kwargs: static_kwargs)
endif
libusb = not_found
if not get_option('libusb').auto() or have_system
  libusb = dependency('libusb-1.0', required: get_option('libusb'),
                      version: '>=1.0.13', method: 'pkg-config',
                      kwargs: static_kwargs)
endif

libpmem = not_found
if not get_option('libpmem').auto() or have_system
  libpmem = dependency('libpmem', required: get_option('libpmem'),
                       method: 'pkg-config', kwargs: static_kwargs)
endif
libdaxctl = not_found
if not get_option('libdaxctl').auto() or have_system
  libdaxctl = dependency('libdaxctl', required: get_option('libdaxctl'),
                         version: '>=57', method: 'pkg-config',
                         kwargs: static_kwargs)
endif
tasn1 = not_found
if gnutls.found()
  tasn1 = dependency('libtasn1',
                     method: 'pkg-config',
                     kwargs: static_kwargs)
endif
keyutils = dependency('libkeyutils', required: false,
                      method: 'pkg-config', kwargs: static_kwargs)

has_gettid = cc.has_function('gettid')

# Malloc tests

malloc = []
if get_option('malloc') == 'system'
  has_malloc_trim = \
    not get_option('malloc_trim').disabled() and \
    cc.links('''#include <malloc.h>
                int main(void) { malloc_trim(0); return 0; }''')
else
  has_malloc_trim = false
  malloc = cc.find_library(get_option('malloc'), required: true)
endif
if not has_malloc_trim and get_option('malloc_trim').enabled()
  if get_option('malloc') == 'system'
    error('malloc_trim not available on this platform.')
  else
    error('malloc_trim not available with non-libc memory allocator')
  endif
endif

# Check whether the glibc provides statx()

gnu_source_prefix = '''
  #ifndef _GNU_SOURCE
  #define _GNU_SOURCE
  #endif
'''
statx_test = gnu_source_prefix + '''
  #include <sys/stat.h>
  int main(void) {
    struct statx statxbuf;
    statx(0, "", 0, STATX_BASIC_STATS, &statxbuf);
    return 0;
  }'''

has_statx = cc.links(statx_test)

have_vhost_user_blk_server = (targetos == 'linux' and
    'CONFIG_VHOST_USER' in config_host)

if get_option('vhost_user_blk_server').enabled()
    if targetos != 'linux'
        error('vhost_user_blk_server requires linux')
    elif 'CONFIG_VHOST_USER' not in config_host
        error('vhost_user_blk_server requires vhost-user support')
    endif
elif get_option('vhost_user_blk_server').disabled() or not have_system
    have_vhost_user_blk_server = false
endif


if get_option('fuse').disabled() and get_option('fuse_lseek').enabled()
  error('Cannot enable fuse-lseek while fuse is disabled')
endif

fuse = dependency('fuse3', required: get_option('fuse'),
                  version: '>=3.1', method: 'pkg-config',
                  kwargs: static_kwargs)

fuse_lseek = not_found
if not get_option('fuse_lseek').disabled()
  if fuse.version().version_compare('>=3.8')
    # Dummy dependency
    fuse_lseek = declare_dependency()
  elif get_option('fuse_lseek').enabled()
    if fuse.found()
      error('fuse-lseek requires libfuse >=3.8, found ' + fuse.version())
    else
      error('fuse-lseek requires libfuse, which was not found')
    endif
  endif
endif

# libbpf
libbpf = dependency('libbpf', required: get_option('bpf'), method: 'pkg-config')
if libbpf.found() and not cc.links('''
   #include <bpf/libbpf.h>
   int main(void)
   {
     bpf_object__destroy_skeleton(NULL);
     return 0;
   }''', dependencies: libbpf)
  libbpf = not_found
  if get_option('bpf').enabled()
    error('libbpf skeleton test failed')
  else
    warning('libbpf skeleton test failed, disabling')
  endif
endif

if get_option('cfi')
  cfi_flags=[]
  # Check for dependency on LTO
  if not get_option('b_lto')
    error('Selected Control-Flow Integrity but LTO is disabled')
  endif
  if config_host.has_key('CONFIG_MODULES')
    error('Selected Control-Flow Integrity is not compatible with modules')
  endif
  # Check for cfi flags. CFI requires LTO so we can't use
  # get_supported_arguments, but need a more complex "compiles" which allows
  # custom arguments
  if cc.compiles('int main () { return 0; }', name: '-fsanitize=cfi-icall',
                 args: ['-flto', '-fsanitize=cfi-icall'] )
    cfi_flags += '-fsanitize=cfi-icall'
  else
    error('-fsanitize=cfi-icall is not supported by the compiler')
  endif
  if cc.compiles('int main () { return 0; }',
                 name: '-fsanitize-cfi-icall-generalize-pointers',
                 args: ['-flto', '-fsanitize=cfi-icall',
                        '-fsanitize-cfi-icall-generalize-pointers'] )
    cfi_flags += '-fsanitize-cfi-icall-generalize-pointers'
  else
    error('-fsanitize-cfi-icall-generalize-pointers is not supported by the compiler')
  endif
  if get_option('cfi_debug')
    if cc.compiles('int main () { return 0; }',
                   name: '-fno-sanitize-trap=cfi-icall',
                   args: ['-flto', '-fsanitize=cfi-icall',
                          '-fno-sanitize-trap=cfi-icall'] )
      cfi_flags += '-fno-sanitize-trap=cfi-icall'
    else
      error('-fno-sanitize-trap=cfi-icall is not supported by the compiler')
    endif
  endif
  add_global_arguments(cfi_flags, native: false, language: ['c', 'cpp', 'objc'])
  add_global_link_arguments(cfi_flags, native: false, language: ['c', 'cpp', 'objc'])
endif

have_host_block_device = (targetos != 'darwin' or
    cc.has_header('IOKit/storage/IOMedia.h'))

#################
# config-host.h #
#################

have_virtfs = (targetos == 'linux' and
    have_system and
    libattr.found() and
    libcap_ng.found())

have_virtfs_proxy_helper = have_virtfs and have_tools

if get_option('virtfs').enabled()
  if not have_virtfs
    if targetos != 'linux'
      error('virtio-9p (virtfs) requires Linux')
    elif not libcap_ng.found() or not libattr.found()
      error('virtio-9p (virtfs) requires libcap-ng-devel and libattr-devel')
    elif not have_system
      error('virtio-9p (virtfs) needs system emulation support')
    endif
  endif
elif get_option('virtfs').disabled()
  have_virtfs = false
endif

config_host_data.set_quoted('CONFIG_BINDIR', get_option('prefix') / get_option('bindir'))
config_host_data.set_quoted('CONFIG_PREFIX', get_option('prefix'))
config_host_data.set_quoted('CONFIG_QEMU_CONFDIR', get_option('prefix') / qemu_confdir)
config_host_data.set_quoted('CONFIG_QEMU_DATADIR', get_option('prefix') / qemu_datadir)
config_host_data.set_quoted('CONFIG_QEMU_DESKTOPDIR', get_option('prefix') / qemu_desktopdir)
config_host_data.set_quoted('CONFIG_QEMU_FIRMWAREPATH', get_option('qemu_firmwarepath'))
config_host_data.set_quoted('CONFIG_QEMU_HELPERDIR', get_option('prefix') / get_option('libexecdir'))
config_host_data.set_quoted('CONFIG_QEMU_ICONDIR', get_option('prefix') / qemu_icondir)
config_host_data.set_quoted('CONFIG_QEMU_LOCALEDIR', get_option('prefix') / get_option('localedir'))
config_host_data.set_quoted('CONFIG_QEMU_LOCALSTATEDIR', get_option('prefix') / get_option('localstatedir'))
config_host_data.set_quoted('CONFIG_QEMU_MODDIR', get_option('prefix') / qemu_moddir)
config_host_data.set_quoted('CONFIG_SYSCONFDIR', get_option('prefix') / get_option('sysconfdir'))

config_host_data.set('CONFIG_ATTR', libattr.found())
config_host_data.set('CONFIG_BRLAPI', brlapi.found())
config_host_data.set('CONFIG_COCOA', cocoa.found())
config_host_data.set('CONFIG_LIBUDEV', libudev.found())
config_host_data.set('CONFIG_LZO', lzo.found())
config_host_data.set('CONFIG_MPATH', mpathpersist.found())
config_host_data.set('CONFIG_MPATH_NEW_API', mpathpersist_new_api)
config_host_data.set('CONFIG_CURL', curl.found())
config_host_data.set('CONFIG_CURSES', curses.found())
config_host_data.set('CONFIG_GBM', gbm.found())
config_host_data.set('CONFIG_GLUSTERFS', glusterfs.found())
if glusterfs.found()
  config_host_data.set('CONFIG_GLUSTERFS_XLATOR_OPT', glusterfs.version().version_compare('>=4'))
  config_host_data.set('CONFIG_GLUSTERFS_DISCARD', glusterfs.version().version_compare('>=5'))
  config_host_data.set('CONFIG_GLUSTERFS_FALLOCATE', glusterfs.version().version_compare('>=6'))
  config_host_data.set('CONFIG_GLUSTERFS_ZEROFILL', glusterfs.version().version_compare('>=6'))
  config_host_data.set('CONFIG_GLUSTERFS_FTRUNCATE_HAS_STAT', glusterfs_ftruncate_has_stat)
  config_host_data.set('CONFIG_GLUSTERFS_IOCB_HAS_STAT', glusterfs_iocb_has_stat)
endif
config_host_data.set('CONFIG_GTK', gtk.found())
config_host_data.set('CONFIG_VTE', vte.found())
config_host_data.set('CONFIG_LIBATTR', have_old_libattr)
config_host_data.set('CONFIG_LIBCAP_NG', libcap_ng.found())
config_host_data.set('CONFIG_EBPF', libbpf.found())
config_host_data.set('CONFIG_LIBDAXCTL', libdaxctl.found())
config_host_data.set('CONFIG_LIBISCSI', libiscsi.found())
config_host_data.set('CONFIG_LIBNFS', libnfs.found())
config_host_data.set('CONFIG_LINUX_IO_URING', linux_io_uring.found())
config_host_data.set('CONFIG_LIBPMEM', libpmem.found())
config_host_data.set('CONFIG_RBD', rbd.found())
config_host_data.set('CONFIG_SDL', sdl.found())
config_host_data.set('CONFIG_SDL_IMAGE', sdl_image.found())
config_host_data.set('CONFIG_SECCOMP', seccomp.found())
config_host_data.set('CONFIG_SNAPPY', snappy.found())
config_host_data.set('CONFIG_USB_LIBUSB', libusb.found())
config_host_data.set('CONFIG_VHOST_USER_BLK_SERVER', have_vhost_user_blk_server)
config_host_data.set('CONFIG_VNC', vnc.found())
config_host_data.set('CONFIG_VNC_JPEG', jpeg.found())
config_host_data.set('CONFIG_VNC_PNG', png.found())
config_host_data.set('CONFIG_VNC_SASL', sasl.found())
config_host_data.set('CONFIG_VIRTFS', have_virtfs)
config_host_data.set('CONFIG_VTE', vte.found())
config_host_data.set('CONFIG_XKBCOMMON', xkbcommon.found())
config_host_data.set('CONFIG_KEYUTILS', keyutils.found())
config_host_data.set('CONFIG_GETTID', has_gettid)
config_host_data.set('CONFIG_GNUTLS', gnutls.found())
config_host_data.set('CONFIG_GNUTLS_CRYPTO', gnutls_crypto.found())
config_host_data.set('CONFIG_GCRYPT', gcrypt.found())
config_host_data.set('CONFIG_NETTLE', nettle.found())
config_host_data.set('CONFIG_QEMU_PRIVATE_XTS', xts == 'private')
config_host_data.set('CONFIG_MALLOC_TRIM', has_malloc_trim)
config_host_data.set('CONFIG_STATX', has_statx)
config_host_data.set('CONFIG_ZSTD', zstd.found())
config_host_data.set('CONFIG_FUSE', fuse.found())
config_host_data.set('CONFIG_FUSE_LSEEK', fuse_lseek.found())
config_host_data.set('CONFIG_X11', x11.found())
config_host_data.set('CONFIG_CFI', get_option('cfi'))
config_host_data.set('QEMU_VERSION', '"@0@"'.format(meson.project_version()))
config_host_data.set('QEMU_VERSION_MAJOR', meson.project_version().split('.')[0])
config_host_data.set('QEMU_VERSION_MINOR', meson.project_version().split('.')[1])
config_host_data.set('QEMU_VERSION_MICRO', meson.project_version().split('.')[2])

config_host_data.set('HAVE_HOST_BLOCK_DEVICE', have_host_block_device)

# has_header
config_host_data.set('CONFIG_EPOLL', cc.has_header('sys/epoll.h'))
config_host_data.set('CONFIG_LINUX_MAGIC_H', cc.has_header('linux/magic.h'))
config_host_data.set('CONFIG_VALGRIND_H', cc.has_header('valgrind/valgrind.h'))
config_host_data.set('HAVE_BTRFS_H', cc.has_header('linux/btrfs.h'))
config_host_data.set('HAVE_DRM_H', cc.has_header('libdrm/drm.h'))
config_host_data.set('HAVE_PTY_H', cc.has_header('pty.h'))
config_host_data.set('HAVE_SYS_DISK_H', cc.has_header('sys/disk.h'))
config_host_data.set('HAVE_SYS_IOCCOM_H', cc.has_header('sys/ioccom.h'))
config_host_data.set('HAVE_SYS_KCOV_H', cc.has_header('sys/kcov.h'))

# has_function
config_host_data.set('CONFIG_ACCEPT4', cc.has_function('accept4'))
config_host_data.set('CONFIG_CLOCK_ADJTIME', cc.has_function('clock_adjtime'))
config_host_data.set('CONFIG_DUP3', cc.has_function('dup3'))
config_host_data.set('CONFIG_FALLOCATE', cc.has_function('fallocate'))
config_host_data.set('CONFIG_POSIX_FALLOCATE', cc.has_function('posix_fallocate'))
config_host_data.set('CONFIG_POSIX_MEMALIGN', cc.has_function('posix_memalign'))
config_host_data.set('CONFIG_PPOLL', cc.has_function('ppoll'))
config_host_data.set('CONFIG_PREADV', cc.has_function('preadv', prefix: '#include <sys/uio.h>'))
config_host_data.set('CONFIG_SEM_TIMEDWAIT', cc.has_function('sem_timedwait', dependencies: threads))
config_host_data.set('CONFIG_SENDFILE', cc.has_function('sendfile'))
config_host_data.set('CONFIG_SETNS', cc.has_function('setns') and cc.has_function('unshare'))
config_host_data.set('CONFIG_SYNCFS', cc.has_function('syncfs'))
config_host_data.set('CONFIG_SYNC_FILE_RANGE', cc.has_function('sync_file_range'))
config_host_data.set('CONFIG_TIMERFD', cc.has_function('timerfd_create'))
config_host_data.set('HAVE_COPY_FILE_RANGE', cc.has_function('copy_file_range'))
config_host_data.set('HAVE_OPENPTY', cc.has_function('openpty', dependencies: util))
config_host_data.set('HAVE_STRCHRNUL', cc.has_function('strchrnul'))
config_host_data.set('HAVE_SYSTEM_FUNCTION', cc.has_function('system', prefix: '#include <stdlib.h>'))

# has_header_symbol
config_host_data.set('CONFIG_BYTESWAP_H',
                     cc.has_header_symbol('byteswap.h', 'bswap_32'))
config_host_data.set('CONFIG_EPOLL_CREATE1',
                     cc.has_header_symbol('sys/epoll.h', 'epoll_create1'))
config_host_data.set('CONFIG_HAS_ENVIRON',
                     cc.has_header_symbol('unistd.h', 'environ', prefix: gnu_source_prefix))
config_host_data.set('CONFIG_FALLOCATE_PUNCH_HOLE',
                     cc.has_header_symbol('linux/falloc.h', 'FALLOC_FL_PUNCH_HOLE') and
                     cc.has_header_symbol('linux/falloc.h', 'FALLOC_FL_KEEP_SIZE'))
config_host_data.set('CONFIG_FALLOCATE_ZERO_RANGE',
                     cc.has_header_symbol('linux/falloc.h', 'FALLOC_FL_ZERO_RANGE'))
config_host_data.set('CONFIG_FIEMAP',
                     cc.has_header('linux/fiemap.h') and
                     cc.has_header_symbol('linux/fs.h', 'FS_IOC_FIEMAP'))
config_host_data.set('CONFIG_GETRANDOM',
                     cc.has_function('getrandom') and
                     cc.has_header_symbol('sys/random.h', 'GRND_NONBLOCK'))
config_host_data.set('CONFIG_INOTIFY',
                     cc.has_header_symbol('sys/inotify.h', 'inotify_init'))
config_host_data.set('CONFIG_INOTIFY1',
                     cc.has_header_symbol('sys/inotify.h', 'inotify_init1'))
config_host_data.set('CONFIG_MACHINE_BSWAP_H',
                     cc.has_header_symbol('machine/bswap.h', 'bswap32',
                                          prefix: '''#include <sys/endian.h>
                                                     #include <sys/types.h>'''))
config_host_data.set('CONFIG_PRCTL_PR_SET_TIMERSLACK',
                     cc.has_header_symbol('sys/prctl.h', 'PR_SET_TIMERSLACK'))
config_host_data.set('CONFIG_RTNETLINK',
                     cc.has_header_symbol('linux/rtnetlink.h', 'IFLA_PROTO_DOWN'))
config_host_data.set('CONFIG_SYSMACROS',
                     cc.has_header_symbol('sys/sysmacros.h', 'makedev'))
config_host_data.set('HAVE_OPTRESET',
                     cc.has_header_symbol('getopt.h', 'optreset'))
config_host_data.set('HAVE_UTMPX',
                     cc.has_header_symbol('utmpx.h', 'struct utmpx'))
config_host_data.set('HAVE_IPPROTO_MPTCP',
                     cc.has_header_symbol('netinet/in.h', 'IPPROTO_MPTCP'))

# has_member
config_host_data.set('HAVE_SIGEV_NOTIFY_THREAD_ID',
                     cc.has_member('struct sigevent', 'sigev_notify_thread_id',
                                   prefix: '#include <signal.h>'))
config_host_data.set('HAVE_STRUCT_STAT_ST_ATIM',
                     cc.has_member('struct stat', 'st_atim',
                                   prefix: '#include <sys/stat.h>'))

config_host_data.set('CONFIG_EVENTFD', cc.links('''
  #include <sys/eventfd.h>
  int main(void) { return eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); }'''))
config_host_data.set('CONFIG_FDATASYNC', cc.links(gnu_source_prefix + '''
  #include <unistd.h>
  int main(void) {
  #if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
  return fdatasync(0);
  #else
  #error Not supported
  #endif
  }'''))
config_host_data.set('CONFIG_MADVISE', cc.links(gnu_source_prefix + '''
  #include <sys/types.h>
  #include <sys/mman.h>
  #include <stddef.h>
  int main(void) { return madvise(NULL, 0, MADV_DONTNEED); }'''))
config_host_data.set('CONFIG_MEMFD', cc.links(gnu_source_prefix + '''
  #include <sys/mman.h>
  int main(void) { return memfd_create("foo", MFD_ALLOW_SEALING); }'''))
config_host_data.set('CONFIG_OPEN_BY_HANDLE', cc.links(gnu_source_prefix + '''
  #include <fcntl.h>
  #if !defined(AT_EMPTY_PATH)
  # error missing definition
  #else
  int main(void) { struct file_handle fh; return open_by_handle_at(0, &fh, 0); }
  #endif'''))
config_host_data.set('CONFIG_PIPE2', cc.links(gnu_source_prefix + '''
  #include <unistd.h>
  #include <fcntl.h>

  int main(void)
  {
      int pipefd[2];
      return pipe2(pipefd, O_CLOEXEC);
  }'''))
config_host_data.set('CONFIG_POSIX_MADVISE', cc.links(gnu_source_prefix + '''
  #include <sys/mman.h>
  #include <stddef.h>
  int main(void) { return posix_madvise(NULL, 0, POSIX_MADV_DONTNEED); }'''))
config_host_data.set('CONFIG_SIGNALFD', cc.links(gnu_source_prefix + '''
  #include <unistd.h>
  #include <sys/syscall.h>
  #include <signal.h>
  int main(void) { return syscall(SYS_signalfd, -1, NULL, _NSIG / 8); }'''))
config_host_data.set('CONFIG_SPLICE', cc.links(gnu_source_prefix + '''
  #include <unistd.h>
  #include <fcntl.h>
  #include <limits.h>

  int main(void)
  {
    int len, fd = 0;
    len = tee(STDIN_FILENO, STDOUT_FILENO, INT_MAX, SPLICE_F_NONBLOCK);
    splice(STDIN_FILENO, NULL, fd, NULL, len, SPLICE_F_MOVE);
    return 0;
  }'''))

# Some versions of Mac OS X incorrectly define SIZE_MAX
config_host_data.set('HAVE_BROKEN_SIZE_MAX', not cc.compiles('''
    #include <stdint.h>
    #include <stdio.h>
    int main(int argc, char *argv[]) {
        return printf("%zu", SIZE_MAX);
    }''', args: ['-Werror']))


ignored = ['CONFIG_QEMU_INTERP_PREFIX'] # actually per-target
arrays = ['CONFIG_AUDIO_DRIVERS', 'CONFIG_BDRV_RW_WHITELIST', 'CONFIG_BDRV_RO_WHITELIST']
strings = ['HOST_DSOSUF', 'CONFIG_IASL']
foreach k, v: config_host
  if ignored.contains(k)
    # do nothing
  elif arrays.contains(k)
    if v != ''
      v = '"' + '", "'.join(v.split()) + '", '
    endif
    config_host_data.set(k, v)
  elif k == 'ARCH'
    config_host_data.set('HOST_' + v.to_upper(), 1)
  elif strings.contains(k)
    if not k.startswith('CONFIG_')
      k = 'CONFIG_' + k.to_upper()
    endif
    config_host_data.set_quoted(k, v)
  elif k.startswith('CONFIG_') or k.startswith('HAVE_') or k.startswith('HOST_')
    config_host_data.set(k, v == 'y' ? 1 : v)
  endif
endforeach

########################
# Target configuration #
########################

minikconf = find_program('scripts/minikconf.py')
config_all = {}
config_all_devices = {}
config_all_disas = {}
config_devices_mak_list = []
config_devices_h = {}
config_target_h = {}
config_target_mak = {}

disassemblers = {
  'alpha' : ['CONFIG_ALPHA_DIS'],
  'arm' : ['CONFIG_ARM_DIS'],
  'avr' : ['CONFIG_AVR_DIS'],
  'cris' : ['CONFIG_CRIS_DIS'],
  'hexagon' : ['CONFIG_HEXAGON_DIS'],
  'hppa' : ['CONFIG_HPPA_DIS'],
  'i386' : ['CONFIG_I386_DIS'],
  'x86_64' : ['CONFIG_I386_DIS'],
  'x32' : ['CONFIG_I386_DIS'],
  'm68k' : ['CONFIG_M68K_DIS'],
  'microblaze' : ['CONFIG_MICROBLAZE_DIS'],
  'mips' : ['CONFIG_MIPS_DIS'],
  'nios2' : ['CONFIG_NIOS2_DIS'],
  'or1k' : ['CONFIG_OPENRISC_DIS'],
  'ppc' : ['CONFIG_PPC_DIS'],
  'riscv' : ['CONFIG_RISCV_DIS'],
  'rx' : ['CONFIG_RX_DIS'],
  's390' : ['CONFIG_S390_DIS'],
  'sh4' : ['CONFIG_SH4_DIS'],
  'sparc' : ['CONFIG_SPARC_DIS'],
  'xtensa' : ['CONFIG_XTENSA_DIS'],
}
if link_language == 'cpp'
  disassemblers += {
    'aarch64' : [ 'CONFIG_ARM_A64_DIS'],
    'arm' : [ 'CONFIG_ARM_DIS', 'CONFIG_ARM_A64_DIS'],
    'mips' : [ 'CONFIG_MIPS_DIS', 'CONFIG_NANOMIPS_DIS'],
  }
endif

have_ivshmem = config_host_data.get('CONFIG_EVENTFD')
host_kconfig = \
  ('CONFIG_TPM' in config_host ? ['CONFIG_TPM=y'] : []) + \
  ('CONFIG_SPICE' in config_host ? ['CONFIG_SPICE=y'] : []) + \
  (have_ivshmem ? ['CONFIG_IVSHMEM=y'] : []) + \
  ('CONFIG_OPENGL' in config_host ? ['CONFIG_OPENGL=y'] : []) + \
  (x11.found() ? ['CONFIG_X11=y'] : []) + \
  ('CONFIG_VHOST_USER' in config_host ? ['CONFIG_VHOST_USER=y'] : []) + \
  ('CONFIG_VHOST_VDPA' in config_host ? ['CONFIG_VHOST_VDPA=y'] : []) + \
  ('CONFIG_VHOST_KERNEL' in config_host ? ['CONFIG_VHOST_KERNEL=y'] : []) + \
  (have_virtfs ? ['CONFIG_VIRTFS=y'] : []) + \
  ('CONFIG_LINUX' in config_host ? ['CONFIG_LINUX=y'] : []) + \
  ('CONFIG_PVRDMA' in config_host ? ['CONFIG_PVRDMA=y'] : []) + \
  (multiprocess_allowed ? ['CONFIG_MULTIPROCESS_ALLOWED=y'] : [])

ignored = [ 'TARGET_XML_FILES', 'TARGET_ABI_DIR', 'TARGET_ARCH' ]

default_targets = 'CONFIG_DEFAULT_TARGETS' in config_host
actual_target_dirs = []
fdt_required = []
foreach target : target_dirs
  config_target = { 'TARGET_NAME': target.split('-')[0] }
  if target.endswith('linux-user')
    if targetos != 'linux'
      if default_targets
        continue
      endif
      error('Target @0@ is only available on a Linux host'.format(target))
    endif
    config_target += { 'CONFIG_LINUX_USER': 'y' }
  elif target.endswith('bsd-user')
    if 'CONFIG_BSD' not in config_host
      if default_targets
        continue
      endif
      error('Target @0@ is only available on a BSD host'.format(target))
    endif
    config_target += { 'CONFIG_BSD_USER': 'y' }
  elif target.endswith('softmmu')
    config_target += { 'CONFIG_SOFTMMU': 'y' }
  endif
  if target.endswith('-user')
    config_target += {
      'CONFIG_USER_ONLY': 'y',
      'CONFIG_QEMU_INTERP_PREFIX':
        config_host['CONFIG_QEMU_INTERP_PREFIX'].format(config_target['TARGET_NAME'])
    }
  endif

  accel_kconfig = []
  foreach sym: accelerators
    if sym == 'CONFIG_TCG' or target in accelerator_targets.get(sym, [])
      config_target += { sym: 'y' }
      config_all += { sym: 'y' }
      if sym == 'CONFIG_TCG' and tcg_arch == 'tci'
        config_target += { 'CONFIG_TCG_INTERPRETER': 'y' }
      elif sym == 'CONFIG_XEN' and have_xen_pci_passthrough
        config_target += { 'CONFIG_XEN_PCI_PASSTHROUGH': 'y' }
      endif
      if target in modular_tcg
        config_target += { 'CONFIG_TCG_MODULAR': 'y' }
      else
        config_target += { 'CONFIG_TCG_BUILTIN': 'y' }
      endif
      accel_kconfig += [ sym + '=y' ]
    endif
  endforeach
  if accel_kconfig.length() == 0
    if default_targets
      continue
    endif
    error('No accelerator available for target @0@'.format(target))
  endif

  actual_target_dirs += target
  config_target += keyval.load('configs/targets' / target + '.mak')
  config_target += { 'TARGET_' + config_target['TARGET_ARCH'].to_upper(): 'y' }

  if 'TARGET_NEED_FDT' in config_target
    fdt_required += target
  endif

  # Add default keys
  if 'TARGET_BASE_ARCH' not in config_target
    config_target += {'TARGET_BASE_ARCH': config_target['TARGET_ARCH']}
  endif
  if 'TARGET_ABI_DIR' not in config_target
    config_target += {'TARGET_ABI_DIR': config_target['TARGET_ARCH']}
  endif

  foreach k, v: disassemblers
    if config_host['ARCH'].startswith(k) or config_target['TARGET_BASE_ARCH'].startswith(k)
      foreach sym: v
        config_target += { sym: 'y' }
        config_all_disas += { sym: 'y' }
      endforeach
    endif
  endforeach

  config_target_data = configuration_data()
  foreach k, v: config_target
    if not k.startswith('TARGET_') and not k.startswith('CONFIG_')
      # do nothing
    elif ignored.contains(k)
      # do nothing
    elif k == 'TARGET_BASE_ARCH'
      # Note that TARGET_BASE_ARCH ends up in config-target.h but it is
      # not used to select files from sourcesets.
      config_target_data.set('TARGET_' + v.to_upper(), 1)
    elif k == 'TARGET_NAME' or k == 'CONFIG_QEMU_INTERP_PREFIX'
      config_target_data.set_quoted(k, v)
    elif v == 'y'
      config_target_data.set(k, 1)
    else
      config_target_data.set(k, v)
    endif
  endforeach
  config_target_data.set('QEMU_ARCH',
                         'QEMU_ARCH_' + config_target['TARGET_BASE_ARCH'].to_upper())
  config_target_h += {target: configure_file(output: target + '-config-target.h',
                                               configuration: config_target_data)}

  if target.endswith('-softmmu')
    config_input = meson.get_external_property(target, 'default')
    config_devices_mak = target + '-config-devices.mak'
    config_devices_mak = configure_file(
      input: ['configs/devices' / target / config_input + '.mak', 'Kconfig'],
      output: config_devices_mak,
      depfile: config_devices_mak + '.d',
      capture: true,
      command: [minikconf,
                get_option('default_devices') ? '--defconfig' : '--allnoconfig',
                config_devices_mak, '@DEPFILE@', '@INPUT@',
                host_kconfig, accel_kconfig,
                'CONFIG_' + config_target['TARGET_ARCH'].to_upper() + '=y'])

    config_devices_data = configuration_data()
    config_devices = keyval.load(config_devices_mak)
    foreach k, v: config_devices
      config_devices_data.set(k, 1)
    endforeach
    config_devices_mak_list += config_devices_mak
    config_devices_h += {target: configure_file(output: target + '-config-devices.h',
                                                configuration: config_devices_data)}
    config_target += config_devices
    config_all_devices += config_devices
  endif
  config_target_mak += {target: config_target}
endforeach
target_dirs = actual_target_dirs

# This configuration is used to build files that are shared by
# multiple binaries, and then extracted out of the "common"
# static_library target.
#
# We do not use all_sources()/all_dependencies(), because it would
# build literally all source files, including devices only used by
# targets that are not built for this compilation.  The CONFIG_ALL
# pseudo symbol replaces it.

config_all += config_all_devices
config_all += config_host
config_all += config_all_disas
config_all += {
  'CONFIG_XEN': config_host.has_key('CONFIG_XEN_BACKEND'),
  'CONFIG_SOFTMMU': have_system,
  'CONFIG_USER_ONLY': have_user,
  'CONFIG_ALL': true,
}

##############
# Submodules #
##############

capstone = not_found
capstone_opt = get_option('capstone')
if capstone_opt in ['enabled', 'auto', 'system']
  have_internal = fs.exists(meson.current_source_dir() / 'capstone/Makefile')
  capstone = dependency('capstone', version: '>=4.0',
                        kwargs: static_kwargs, method: 'pkg-config',
                        required: capstone_opt == 'system' or
                                  capstone_opt == 'enabled' and not have_internal)

  # Some versions of capstone have broken pkg-config file
  # that reports a wrong -I path, causing the #include to
  # fail later. If the system has such a broken version
  # do not use it.
  if capstone.found() and not cc.compiles('#include <capstone.h>',
                                          dependencies: [capstone])
    capstone = not_found
    if capstone_opt == 'system'
      error('system capstone requested, it does not appear to work')
    endif
  endif

  if capstone.found()
    capstone_opt = 'system'
  elif have_internal
    capstone_opt = 'internal'
  else
    capstone_opt = 'disabled'
  endif
endif
if capstone_opt == 'internal'
  capstone_data = configuration_data()
  capstone_data.set('CAPSTONE_USE_SYS_DYN_MEM', '1')

  capstone_files = files(
    'capstone/cs.c',
    'capstone/MCInst.c',
    'capstone/MCInstrDesc.c',
    'capstone/MCRegisterInfo.c',
    'capstone/SStream.c',
    'capstone/utils.c'
  )

  if 'CONFIG_ARM_DIS' in config_all_disas
    capstone_data.set('CAPSTONE_HAS_ARM', '1')
    capstone_files += files(
      'capstone/arch/ARM/ARMDisassembler.c',
      'capstone/arch/ARM/ARMInstPrinter.c',
      'capstone/arch/ARM/ARMMapping.c',
      'capstone/arch/ARM/ARMModule.c'
    )
  endif

  # FIXME: This config entry currently depends on a c++ compiler.
  # Which is needed for building libvixl, but not for capstone.
  if 'CONFIG_ARM_A64_DIS' in config_all_disas
    capstone_data.set('CAPSTONE_HAS_ARM64', '1')
    capstone_files += files(
      'capstone/arch/AArch64/AArch64BaseInfo.c',
      'capstone/arch/AArch64/AArch64Disassembler.c',
      'capstone/arch/AArch64/AArch64InstPrinter.c',
      'capstone/arch/AArch64/AArch64Mapping.c',
      'capstone/arch/AArch64/AArch64Module.c'
    )
  endif

  if 'CONFIG_PPC_DIS' in config_all_disas
    capstone_data.set('CAPSTONE_HAS_POWERPC', '1')
    capstone_files += files(
      'capstone/arch/PowerPC/PPCDisassembler.c',
      'capstone/arch/PowerPC/PPCInstPrinter.c',
      'capstone/arch/PowerPC/PPCMapping.c',
      'capstone/arch/PowerPC/PPCModule.c'
    )
  endif

  if 'CONFIG_S390_DIS' in config_all_disas
    capstone_data.set('CAPSTONE_HAS_SYSZ', '1')
    capstone_files += files(
      'capstone/arch/SystemZ/SystemZDisassembler.c',
      'capstone/arch/SystemZ/SystemZInstPrinter.c',
      'capstone/arch/SystemZ/SystemZMapping.c',
      'capstone/arch/SystemZ/SystemZModule.c',
      'capstone/arch/SystemZ/SystemZMCTargetDesc.c'
    )
  endif

  if 'CONFIG_I386_DIS' in config_all_disas
    capstone_data.set('CAPSTONE_HAS_X86', 1)
    capstone_files += files(
      'capstone/arch/X86/X86Disassembler.c',
      'capstone/arch/X86/X86DisassemblerDecoder.c',
      'capstone/arch/X86/X86ATTInstPrinter.c',
      'capstone/arch/X86/X86IntelInstPrinter.c',
      'capstone/arch/X86/X86InstPrinterCommon.c',
      'capstone/arch/X86/X86Mapping.c',
      'capstone/arch/X86/X86Module.c'
    )
  endif

  configure_file(output: 'capstone-defs.h', configuration: capstone_data)

  capstone_cargs = [
    # FIXME: There does not seem to be a way to completely replace the c_args
    # that come from add_project_arguments() -- we can only add to them.
    # So: disable all warnings with a big hammer.
    '-Wno-error', '-w',

    # Include all configuration defines via a header file, which will wind up
    # as a dependency on the object file, and thus changes here will result
    # in a rebuild.
    '-include', 'capstone-defs.h'
  ]

  libcapstone = static_library('capstone',
                               build_by_default: false,
                               sources: capstone_files,
                               c_args: capstone_cargs,
                               include_directories: 'capstone/include')
  capstone = declare_dependency(link_with: libcapstone,
                                include_directories: 'capstone/include/capstone')
endif

slirp = not_found
slirp_opt = 'disabled'
if have_system
  slirp_opt = get_option('slirp')
  if slirp_opt in ['enabled', 'auto', 'system']
    have_internal = fs.exists(meson.current_source_dir() / 'slirp/meson.build')
    slirp = dependency('slirp', kwargs: static_kwargs,
                       method: 'pkg-config',
                       required: slirp_opt == 'system' or
                                 slirp_opt == 'enabled' and not have_internal)
    if slirp.found()
      slirp_opt = 'system'
    elif have_internal
      slirp_opt = 'internal'
    else
      slirp_opt = 'disabled'
    endif
  endif
  if slirp_opt == 'internal'
    slirp_deps = []
    if targetos == 'windows'
      slirp_deps = cc.find_library('iphlpapi')
    elif targetos == 'darwin'
      slirp_deps = cc.find_library('resolv')
    endif
    slirp_conf = configuration_data()
    slirp_conf.set('SLIRP_MAJOR_VERSION', meson.project_version().split('.')[0])
    slirp_conf.set('SLIRP_MINOR_VERSION', meson.project_version().split('.')[1])
    slirp_conf.set('SLIRP_MICRO_VERSION', meson.project_version().split('.')[2])
    slirp_conf.set_quoted('SLIRP_VERSION_STRING', meson.project_version())
    slirp_cargs = ['-DG_LOG_DOMAIN="Slirp"']
    slirp_files = [
      'slirp/src/arp_table.c',
      'slirp/src/bootp.c',
      'slirp/src/cksum.c',
      'slirp/src/dhcpv6.c',
      'slirp/src/dnssearch.c',
      'slirp/src/if.c',
      'slirp/src/ip6_icmp.c',
      'slirp/src/ip6_input.c',
      'slirp/src/ip6_output.c',
      'slirp/src/ip_icmp.c',
      'slirp/src/ip_input.c',
      'slirp/src/ip_output.c',
      'slirp/src/mbuf.c',
      'slirp/src/misc.c',
      'slirp/src/ncsi.c',
      'slirp/src/ndp_table.c',
      'slirp/src/sbuf.c',
      'slirp/src/slirp.c',
      'slirp/src/socket.c',
      'slirp/src/state.c',
      'slirp/src/stream.c',
      'slirp/src/tcp_input.c',
      'slirp/src/tcp_output.c',
      'slirp/src/tcp_subr.c',
      'slirp/src/tcp_timer.c',
      'slirp/src/tftp.c',
      'slirp/src/udp.c',
      'slirp/src/udp6.c',
      'slirp/src/util.c',
      'slirp/src/version.c',
      'slirp/src/vmstate.c',
    ]

    configure_file(
      input : 'slirp/src/libslirp-version.h.in',
      output : 'libslirp-version.h',
      configuration: slirp_conf)

    slirp_inc = include_directories('slirp', 'slirp/src')
    libslirp = static_library('slirp',
                              build_by_default: false,
                              sources: slirp_files,
                              c_args: slirp_cargs,
                              include_directories: slirp_inc)
    slirp = declare_dependency(link_with: libslirp,
                               dependencies: slirp_deps,
                               include_directories: slirp_inc)
  endif
endif

# For CFI, we need to compile slirp as a static library together with qemu.
# This is because we register slirp functions as callbacks for QEMU Timers.
# When using a system-wide shared libslirp, the type information for the
# callback is missing and the timer call produces a false positive with CFI.
#
# Now that slirp_opt has been defined, check if the selected slirp is compatible
# with control-flow integrity.
if get_option('cfi') and slirp_opt == 'system'
  error('Control-Flow Integrity is not compatible with system-wide slirp.' \
         + ' Please configure with --enable-slirp=git')
endif

fdt = not_found
fdt_opt = get_option('fdt')
if have_system
  if fdt_opt in ['enabled', 'auto', 'system']
    have_internal = fs.exists(meson.current_source_dir() / 'dtc/libfdt/Makefile.libfdt')
    fdt = cc.find_library('fdt', kwargs: static_kwargs,
                          required: fdt_opt == 'system' or
                                    fdt_opt == 'enabled' and not have_internal)
    if fdt.found() and cc.links('''
       #include <libfdt.h>
       #include <libfdt_env.h>
       int main(void) { fdt_check_full(NULL, 0); return 0; }''',
         dependencies: fdt)
      fdt_opt = 'system'
    elif fdt_opt == 'system'
       error('system libfdt requested, but it is too old (1.5.1 or newer required)')
    elif have_internal
      fdt_opt = 'internal'
    else
      fdt_opt = 'disabled'
      fdt = not_found
    endif
  endif
  if fdt_opt == 'internal'
    fdt_files = files(
      'dtc/libfdt/fdt.c',
      'dtc/libfdt/fdt_ro.c',
      'dtc/libfdt/fdt_wip.c',
      'dtc/libfdt/fdt_sw.c',
      'dtc/libfdt/fdt_rw.c',
      'dtc/libfdt/fdt_strerror.c',
      'dtc/libfdt/fdt_empty_tree.c',
      'dtc/libfdt/fdt_addresses.c',
      'dtc/libfdt/fdt_overlay.c',
      'dtc/libfdt/fdt_check.c',
    )

    fdt_inc = include_directories('dtc/libfdt')
    libfdt = static_library('fdt',
                            build_by_default: false,
                            sources: fdt_files,
                            include_directories: fdt_inc)
    fdt = declare_dependency(link_with: libfdt,
                             include_directories: fdt_inc)
  endif
endif
if not fdt.found() and fdt_required.length() > 0
  error('fdt not available but required by targets ' + ', '.join(fdt_required))
endif

config_host_data.set('CONFIG_CAPSTONE', capstone.found())
config_host_data.set('CONFIG_FDT', fdt.found())
config_host_data.set('CONFIG_SLIRP', slirp.found())

#####################
# Generated sources #
#####################

genh += configure_file(output: 'config-host.h', configuration: config_host_data)

hxtool = find_program('scripts/hxtool')
shaderinclude = find_program('scripts/shaderinclude.pl')
qapi_gen = find_program('scripts/qapi-gen.py')
qapi_gen_depends = [ meson.current_source_dir() / 'scripts/qapi/__init__.py',
                     meson.current_source_dir() / 'scripts/qapi/commands.py',
                     meson.current_source_dir() / 'scripts/qapi/common.py',
                     meson.current_source_dir() / 'scripts/qapi/error.py',
                     meson.current_source_dir() / 'scripts/qapi/events.py',
                     meson.current_source_dir() / 'scripts/qapi/expr.py',
                     meson.current_source_dir() / 'scripts/qapi/gen.py',
                     meson.current_source_dir() / 'scripts/qapi/introspect.py',
                     meson.current_source_dir() / 'scripts/qapi/parser.py',
                     meson.current_source_dir() / 'scripts/qapi/schema.py',
                     meson.current_source_dir() / 'scripts/qapi/source.py',
                     meson.current_source_dir() / 'scripts/qapi/types.py',
                     meson.current_source_dir() / 'scripts/qapi/visit.py',
                     meson.current_source_dir() / 'scripts/qapi/common.py',
                     meson.current_source_dir() / 'scripts/qapi-gen.py'
]

tracetool = [
  python, files('scripts/tracetool.py'),
   '--backend=' + config_host['TRACE_BACKENDS']
]
tracetool_depends = files(
  'scripts/tracetool/backend/log.py',
  'scripts/tracetool/backend/__init__.py',
  'scripts/tracetool/backend/dtrace.py',
  'scripts/tracetool/backend/ftrace.py',
  'scripts/tracetool/backend/simple.py',
  'scripts/tracetool/backend/syslog.py',
  'scripts/tracetool/backend/ust.py',
  'scripts/tracetool/format/tcg_h.py',
  'scripts/tracetool/format/ust_events_c.py',
  'scripts/tracetool/format/ust_events_h.py',
  'scripts/tracetool/format/__init__.py',
  'scripts/tracetool/format/d.py',
  'scripts/tracetool/format/tcg_helper_c.py',
  'scripts/tracetool/format/simpletrace_stap.py',
  'scripts/tracetool/format/c.py',
  'scripts/tracetool/format/h.py',
  'scripts/tracetool/format/tcg_helper_h.py',
  'scripts/tracetool/format/log_stap.py',
  'scripts/tracetool/format/stap.py',
  'scripts/tracetool/format/tcg_helper_wrapper_h.py',
  'scripts/tracetool/__init__.py',
  'scripts/tracetool/transform.py',
  'scripts/tracetool/vcpu.py'
)

qemu_version_cmd = [find_program('scripts/qemu-version.sh'),
                    meson.current_source_dir(),
                    config_host['PKGVERSION'], meson.project_version()]
qemu_version = custom_target('qemu-version.h',
                             output: 'qemu-version.h',
                             command: qemu_version_cmd,
                             capture: true,
                             build_by_default: true,
                             build_always_stale: true)
genh += qemu_version

hxdep = []
hx_headers = [
  ['qemu-options.hx', 'qemu-options.def'],
  ['qemu-img-cmds.hx', 'qemu-img-cmds.h'],
]
if have_system
  hx_headers += [
    ['hmp-commands.hx', 'hmp-commands.h'],
    ['hmp-commands-info.hx', 'hmp-commands-info.h'],
  ]
endif
foreach d : hx_headers
  hxdep += custom_target(d[1],
                input: files(d[0]),
                output: d[1],
                capture: true,
                build_by_default: true, # to be removed when added to a target
                command: [hxtool, '-h', '@INPUT0@'])
endforeach
genh += hxdep

###################
# Collect sources #
###################

authz_ss = ss.source_set()
blockdev_ss = ss.source_set()
block_ss = ss.source_set()
bsd_user_ss = ss.source_set()
chardev_ss = ss.source_set()
common_ss = ss.source_set()
crypto_ss = ss.source_set()
io_ss = ss.source_set()
linux_user_ss = ss.source_set()
qmp_ss = ss.source_set()
qom_ss = ss.source_set()
softmmu_ss = ss.source_set()
specific_fuzz_ss = ss.source_set()
specific_ss = ss.source_set()
stub_ss = ss.source_set()
trace_ss = ss.source_set()
user_ss = ss.source_set()
util_ss = ss.source_set()

# accel modules
qtest_module_ss = ss.source_set()
tcg_module_ss = ss.source_set()

modules = {}
target_modules = {}
hw_arch = {}
target_arch = {}
target_softmmu_arch = {}
target_user_arch = {}

###############
# Trace files #
###############

# TODO: add each directory to the subdirs from its own meson.build, once
# we have those
trace_events_subdirs = [
  'crypto',
  'qapi',
  'qom',
  'monitor',
  'util',
]
if have_user
  trace_events_subdirs += [ 'linux-user' ]
endif
if have_block
  trace_events_subdirs += [
    'authz',
    'block',
    'io',
    'nbd',
    'scsi',
  ]
endif
if have_system
  trace_events_subdirs += [
    'accel/kvm',
    'audio',
    'backends',
    'backends/tpm',
    'chardev',
    'ebpf',
    'hw/9pfs',
    'hw/acpi',
    'hw/adc',
    'hw/alpha',
    'hw/arm',
    'hw/audio',
    'hw/block',
    'hw/block/dataplane',
    'hw/char',
    'hw/display',
    'hw/dma',
    'hw/hppa',
    'hw/hyperv',
    'hw/i2c',
    'hw/i386',
    'hw/i386/xen',
    'hw/ide',
    'hw/input',
    'hw/intc',
    'hw/isa',
    'hw/mem',
    'hw/mips',
    'hw/misc',
    'hw/misc/macio',
    'hw/net',
    'hw/net/can',
    'hw/nubus',
    'hw/nvme',
    'hw/nvram',
    'hw/pci',
    'hw/pci-host',
    'hw/ppc',
    'hw/rdma',
    'hw/rdma/vmw',
    'hw/rtc',
    'hw/s390x',
    'hw/scsi',
    'hw/sd',
    'hw/sparc',
    'hw/sparc64',
    'hw/ssi',
    'hw/timer',
    'hw/tpm',
    'hw/usb',
    'hw/vfio',
    'hw/virtio',
    'hw/watchdog',
    'hw/xen',
    'hw/gpio',
    'migration',
    'net',
    'softmmu',
    'ui',
    'hw/remote',
  ]
endif
if have_system or have_user
  trace_events_subdirs += [
    'accel/tcg',
    'hw/core',
    'target/arm',
    'target/arm/hvf',
    'target/hppa',
    'target/i386',
    'target/i386/kvm',
    'target/mips/tcg',
    'target/ppc',
    'target/riscv',
    'target/s390x',
    'target/s390x/kvm',
    'target/sparc',
  ]
endif

vhost_user = not_found
if 'CONFIG_VHOST_USER' in config_host
  libvhost_user = subproject('libvhost-user')
  vhost_user = libvhost_user.get_variable('vhost_user_dep')
endif

subdir('qapi')
subdir('qobject')
subdir('stubs')
subdir('trace')
subdir('util')
subdir('qom')
subdir('authz')
subdir('crypto')
subdir('ui')


if enable_modules
  libmodulecommon = static_library('module-common', files('module-common.c') + genh, pic: true, c_args: '-DBUILD_DSO')
  modulecommon = declare_dependency(link_whole: libmodulecommon, compile_args: '-DBUILD_DSO')
endif

stub_ss = stub_ss.apply(config_all, strict: false)

util_ss.add_all(trace_ss)
util_ss = util_ss.apply(config_all, strict: false)
libqemuutil = static_library('qemuutil',
                             sources: util_ss.sources() + stub_ss.sources() + genh,
                             dependencies: [util_ss.dependencies(), libm, threads, glib, socket, malloc, pixman])
qemuutil = declare_dependency(link_with: libqemuutil,
                              sources: genh + version_res)

if have_system or have_user
  decodetree = generator(find_program('scripts/decodetree.py'),
                         output: 'decode-@BASENAME@.c.inc',
                         arguments: ['@INPUT@', '@EXTRA_ARGS@', '-o', '@OUTPUT@'])
  subdir('libdecnumber')
  subdir('target')
endif

subdir('audio')
subdir('io')
subdir('chardev')
subdir('fsdev')
subdir('dump')

if have_block
  block_ss.add(files(
    'block.c',
    'blockjob.c',
    'job.c',
    'qemu-io-cmds.c',
  ))
  block_ss.add(when: 'CONFIG_REPLICATION', if_true: files('replication.c'))

  subdir('nbd')
  subdir('scsi')
  subdir('block')

  blockdev_ss.add(files(
    'blockdev.c',
    'blockdev-nbd.c',
    'iothread.c',
    'job-qmp.c',
  ), gnutls)

  # os-posix.c contains POSIX-specific functions used by qemu-storage-daemon,
  # os-win32.c does not
  blockdev_ss.add(when: 'CONFIG_POSIX', if_true: files('os-posix.c'))
  softmmu_ss.add(when: 'CONFIG_WIN32', if_true: [files('os-win32.c')])
endif

common_ss.add(files('cpus-common.c'))

subdir('softmmu')

common_ss.add(capstone)
specific_ss.add(files('cpu.c', 'disas.c', 'gdbstub.c'), capstone)

# Work around a gcc bug/misfeature wherein constant propagation looks
# through an alias:
#   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=99696
# to guess that a const variable is always zero.  Without lto, this is
# impossible, as the alias is restricted to page-vary-common.c.  Indeed,
# without lto, not even the alias is required -- we simply use different
# declarations in different compilation units.
pagevary = files('page-vary-common.c')
if get_option('b_lto')
  pagevary_flags = ['-fno-lto']
  if get_option('cfi')
    pagevary_flags += '-fno-sanitize=cfi-icall'
  endif
  pagevary = static_library('page-vary-common', sources: pagevary,
                            c_args: pagevary_flags)
  pagevary = declare_dependency(link_with: pagevary)
endif
common_ss.add(pagevary)
specific_ss.add(files('page-vary.c'))

subdir('backends')
subdir('disas')
subdir('migration')
subdir('monitor')
subdir('net')
subdir('replay')
subdir('semihosting')
subdir('hw')
subdir('tcg')
subdir('fpu')
subdir('accel')
subdir('plugins')
subdir('bsd-user')
subdir('linux-user')
subdir('ebpf')

common_ss.add(libbpf)

bsd_user_ss.add(files('gdbstub.c'))
specific_ss.add_all(when: 'CONFIG_BSD_USER', if_true: bsd_user_ss)

linux_user_ss.add(files('gdbstub.c', 'thunk.c'))
specific_ss.add_all(when: 'CONFIG_LINUX_USER', if_true: linux_user_ss)

# needed for fuzzing binaries
subdir('tests/qtest/libqos')
subdir('tests/qtest/fuzz')

# accel modules
tcg_real_module_ss = ss.source_set()
tcg_real_module_ss.add_all(when: 'CONFIG_TCG_MODULAR', if_true: tcg_module_ss)
specific_ss.add_all(when: 'CONFIG_TCG_BUILTIN', if_true: tcg_module_ss)
target_modules += { 'accel' : { 'qtest': qtest_module_ss,
                                'tcg': tcg_real_module_ss }}

########################
# Library dependencies #
########################

modinfo_collect = find_program('scripts/modinfo-collect.py')
modinfo_generate = find_program('scripts/modinfo-generate.py')
modinfo_files = []

block_mods = []
softmmu_mods = []
foreach d, list : modules
  foreach m, module_ss : list
    if enable_modules and targetos != 'windows'
      module_ss = module_ss.apply(config_all, strict: false)
      sl = static_library(d + '-' + m, [genh, module_ss.sources()],
                          dependencies: [modulecommon, module_ss.dependencies()], pic: true)
      if d == 'block'
        block_mods += sl
      else
        softmmu_mods += sl
      endif
      if module_ss.sources() != []
        # FIXME: Should use sl.extract_all_objects(recursive: true) as
        # input. Sources can be used multiple times but objects are
        # unique when it comes to lookup in compile_commands.json.
        # Depnds on a mesion version with
        # https://github.com/mesonbuild/meson/pull/8900
        modinfo_files += custom_target(d + '-' + m + '.modinfo',
                                       output: d + '-' + m + '.modinfo',
                                       input: module_ss.sources() + genh,
                                       capture: true,
                                       command: [modinfo_collect, module_ss.sources()])
      endif
    else
      if d == 'block'
        block_ss.add_all(module_ss)
      else
        softmmu_ss.add_all(module_ss)
      endif
    endif
  endforeach
endforeach

foreach d, list : target_modules
  foreach m, module_ss : list
    if enable_modules and targetos != 'windows'
      foreach target : target_dirs
        if target.endswith('-softmmu')
          config_target = config_target_mak[target]
          config_target += config_host
          target_inc = [include_directories('target' / config_target['TARGET_BASE_ARCH'])]
          c_args = ['-DNEED_CPU_H',
                    '-DCONFIG_TARGET="@0@-config-target.h"'.format(target),
                    '-DCONFIG_DEVICES="@0@-config-devices.h"'.format(target)]
          target_module_ss = module_ss.apply(config_target, strict: false)
          if target_module_ss.sources() != []
            module_name = d + '-' + m + '-' + config_target['TARGET_NAME']
            sl = static_library(module_name,
                                [genh, target_module_ss.sources()],
                                dependencies: [modulecommon, target_module_ss.dependencies()],
                                include_directories: target_inc,
                                c_args: c_args,
                                pic: true)
            softmmu_mods += sl
            # FIXME: Should use sl.extract_all_objects(recursive: true) too.
            modinfo_files += custom_target(module_name + '.modinfo',
                                           output: module_name + '.modinfo',
                                           input: target_module_ss.sources() + genh,
                                           capture: true,
                                           command: [modinfo_collect, '--target', target, target_module_ss.sources()])
          endif
        endif
      endforeach
    else
      specific_ss.add_all(module_ss)
    endif
  endforeach
endforeach

if enable_modules
  modinfo_src = custom_target('modinfo.c',
                              output: 'modinfo.c',
                              input: modinfo_files,
                              command: [modinfo_generate, '@INPUT@'],
                              capture: true)
  modinfo_lib = static_library('modinfo', modinfo_src)
  modinfo_dep = declare_dependency(link_whole: modinfo_lib)
  softmmu_ss.add(modinfo_dep)
endif

nm = find_program('nm')
undefsym = find_program('scripts/undefsym.py')
block_syms = custom_target('block.syms', output: 'block.syms',
                             input: [libqemuutil, block_mods],
                             capture: true,
                             command: [undefsym, nm, '@INPUT@'])
qemu_syms = custom_target('qemu.syms', output: 'qemu.syms',
                             input: [libqemuutil, softmmu_mods],
                             capture: true,
                             command: [undefsym, nm, '@INPUT@'])

qom_ss = qom_ss.apply(config_host, strict: false)
libqom = static_library('qom', qom_ss.sources() + genh,
                        dependencies: [qom_ss.dependencies()],
                        name_suffix: 'fa')

qom = declare_dependency(link_whole: libqom)

authz_ss = authz_ss.apply(config_host, strict: false)
libauthz = static_library('authz', authz_ss.sources() + genh,
                          dependencies: [authz_ss.dependencies()],
                          name_suffix: 'fa',
                          build_by_default: false)

authz = declare_dependency(link_whole: libauthz,
                           dependencies: qom)

crypto_ss = crypto_ss.apply(config_host, strict: false)
libcrypto = static_library('crypto', crypto_ss.sources() + genh,
                           dependencies: [crypto_ss.dependencies()],
                           name_suffix: 'fa',
                           build_by_default: false)

crypto = declare_dependency(link_whole: libcrypto,
                            dependencies: [authz, qom])

io_ss = io_ss.apply(config_host, strict: false)
libio = static_library('io', io_ss.sources() + genh,
                       dependencies: [io_ss.dependencies()],
                       link_with: libqemuutil,
                       name_suffix: 'fa',
                       build_by_default: false)

io = declare_dependency(link_whole: libio, dependencies: [crypto, qom])

libmigration = static_library('migration', sources: migration_files + genh,
                              name_suffix: 'fa',
                              build_by_default: false)
migration = declare_dependency(link_with: libmigration,
                               dependencies: [zlib, qom, io])
softmmu_ss.add(migration)

block_ss = block_ss.apply(config_host, strict: false)
libblock = static_library('block', block_ss.sources() + genh,
                          dependencies: block_ss.dependencies(),
                          link_depends: block_syms,
                          name_suffix: 'fa',
                          build_by_default: false)

block = declare_dependency(link_whole: [libblock],
                           link_args: '@block.syms',
                           dependencies: [crypto, io])

blockdev_ss = blockdev_ss.apply(config_host, strict: false)
libblockdev = static_library('blockdev', blockdev_ss.sources() + genh,
                             dependencies: blockdev_ss.dependencies(),
                             name_suffix: 'fa',
                             build_by_default: false)

blockdev = declare_dependency(link_whole: [libblockdev],
                              dependencies: [block])

qmp_ss = qmp_ss.apply(config_host, strict: false)
libqmp = static_library('qmp', qmp_ss.sources() + genh,
                        dependencies: qmp_ss.dependencies(),
                        name_suffix: 'fa',
                        build_by_default: false)

qmp = declare_dependency(link_whole: [libqmp])

libchardev = static_library('chardev', chardev_ss.sources() + genh,
                            name_suffix: 'fa',
                            dependencies: [gnutls],
                            build_by_default: false)

chardev = declare_dependency(link_whole: libchardev)

libhwcore = static_library('hwcore', sources: hwcore_files + genh,
                           name_suffix: 'fa',
                           build_by_default: false)
hwcore = declare_dependency(link_whole: libhwcore)
common_ss.add(hwcore)

###########
# Targets #
###########

foreach m : block_mods + softmmu_mods
  shared_module(m.name(),
                name_prefix: '',
                link_whole: m,
                install: true,
                install_dir: qemu_moddir)
endforeach

softmmu_ss.add(authz, blockdev, chardev, crypto, io, qmp)
common_ss.add(qom, qemuutil)

common_ss.add_all(when: 'CONFIG_SOFTMMU', if_true: [softmmu_ss])
common_ss.add_all(when: 'CONFIG_USER_ONLY', if_true: user_ss)

common_all = common_ss.apply(config_all, strict: false)
common_all = static_library('common',
                            build_by_default: false,
                            sources: common_all.sources() + genh,
                            implicit_include_directories: false,
                            dependencies: common_all.dependencies(),
                            name_suffix: 'fa')

feature_to_c = find_program('scripts/feature_to_c.sh')

emulators = {}
foreach target : target_dirs
  config_target = config_target_mak[target]
  target_name = config_target['TARGET_NAME']
  arch = config_target['TARGET_BASE_ARCH']
  arch_srcs = [config_target_h[target]]
  arch_deps = []
  c_args = ['-DNEED_CPU_H',
            '-DCONFIG_TARGET="@0@-config-target.h"'.format(target),
            '-DCONFIG_DEVICES="@0@-config-devices.h"'.format(target)]
  link_args = emulator_link_args

  config_target += config_host
  target_inc = [include_directories('target' / config_target['TARGET_BASE_ARCH'])]
  if targetos == 'linux'
    target_inc += include_directories('linux-headers', is_system: true)
  endif
  if target.endswith('-softmmu')
    qemu_target_name = 'qemu-system-' + target_name
    target_type='system'
    t = target_softmmu_arch[arch].apply(config_target, strict: false)
    arch_srcs += t.sources()
    arch_deps += t.dependencies()

    hw_dir = target_name == 'sparc64' ? 'sparc64' : arch
    hw = hw_arch[hw_dir].apply(config_target, strict: false)
    arch_srcs += hw.sources()
    arch_deps += hw.dependencies()

    arch_srcs += config_devices_h[target]
    link_args += ['@block.syms', '@qemu.syms']
  else
    abi = config_target['TARGET_ABI_DIR']
    target_type='user'
    qemu_target_name = 'qemu-' + target_name
    if arch in target_user_arch
      t = target_user_arch[arch].apply(config_target, strict: false)
      arch_srcs += t.sources()
      arch_deps += t.dependencies()
    endif
    if 'CONFIG_LINUX_USER' in config_target
      base_dir = 'linux-user'
      target_inc += include_directories('linux-user/host/' / config_host['ARCH'])
    endif
    if 'CONFIG_BSD_USER' in config_target
      base_dir = 'bsd-user'
      target_inc += include_directories('bsd-user/' / targetos)
      dir = base_dir / abi
      arch_srcs += files(dir / 'target_arch_cpu.c')
    endif
    target_inc += include_directories(
      base_dir,
      base_dir / abi,
    )
    if 'CONFIG_LINUX_USER' in config_target
      dir = base_dir / abi
      arch_srcs += files(dir / 'signal.c', dir / 'cpu_loop.c')
      if config_target.has_key('TARGET_SYSTBL_ABI')
        arch_srcs += \
          syscall_nr_generators[abi].process(base_dir / abi / config_target['TARGET_SYSTBL'],
                                             extra_args : config_target['TARGET_SYSTBL_ABI'])
      endif
    endif
  endif

  if 'TARGET_XML_FILES' in config_target
    gdbstub_xml = custom_target(target + '-gdbstub-xml.c',
                                output: target + '-gdbstub-xml.c',
                                input: files(config_target['TARGET_XML_FILES'].split()),
                                command: [feature_to_c, '@INPUT@'],
                                capture: true)
    arch_srcs += gdbstub_xml
  endif

  t = target_arch[arch].apply(config_target, strict: false)
  arch_srcs += t.sources()
  arch_deps += t.dependencies()

  target_common = common_ss.apply(config_target, strict: false)
  objects = common_all.extract_objects(target_common.sources())
  deps = target_common.dependencies()

  target_specific = specific_ss.apply(config_target, strict: false)
  arch_srcs += target_specific.sources()
  arch_deps += target_specific.dependencies()

  lib = static_library('qemu-' + target,
                 sources: arch_srcs + genh,
                 dependencies: arch_deps,
                 objects: objects,
                 include_directories: target_inc,
                 c_args: c_args,
                 build_by_default: false,
                 name_suffix: 'fa')

  if target.endswith('-softmmu')
    execs = [{
      'name': 'qemu-system-' + target_name,
      'win_subsystem': 'console',
      'sources': files('softmmu/main.c'),
      'dependencies': []
    }]
    if targetos == 'windows' and (sdl.found() or gtk.found())
      execs += [{
        'name': 'qemu-system-' + target_name + 'w',
        'win_subsystem': 'windows',
        'sources': files('softmmu/main.c'),
        'dependencies': []
      }]
    endif
    if config_host.has_key('CONFIG_FUZZ')
      specific_fuzz = specific_fuzz_ss.apply(config_target, strict: false)
      execs += [{
        'name': 'qemu-fuzz-' + target_name,
        'win_subsystem': 'console',
        'sources': specific_fuzz.sources(),
        'dependencies': specific_fuzz.dependencies(),
      }]
    endif
  else
    execs = [{
      'name': 'qemu-' + target_name,
      'win_subsystem': 'console',
      'sources': [],
      'dependencies': []
    }]
  endif
  foreach exe: execs
    exe_name = exe['name']
    if targetos == 'darwin'
      exe_name += '-unsigned'
    endif

    emulator = executable(exe_name, exe['sources'],
               install: true,
               c_args: c_args,
               dependencies: arch_deps + deps + exe['dependencies'],
               objects: lib.extract_all_objects(recursive: true),
               link_language: link_language,
               link_depends: [block_syms, qemu_syms] + exe.get('link_depends', []),
               link_args: link_args,
               win_subsystem: exe['win_subsystem'])

    if targetos == 'darwin'
      icon = 'pc-bios/qemu.rsrc'
      build_input = [emulator, files(icon)]
      install_input = [
        get_option('bindir') / exe_name,
        meson.current_source_dir() / icon
      ]
      if 'CONFIG_HVF' in config_target
        entitlements = 'accel/hvf/entitlements.plist'
        build_input += files(entitlements)
        install_input += meson.current_source_dir() / entitlements
      endif

      emulators += {exe['name'] : custom_target(exe['name'],
                   input: build_input,
                   output: exe['name'],
                   command: [
                     files('scripts/entitlement.sh'),
                     '@OUTPUT@',
                     '@INPUT@'
                   ])
      }

      meson.add_install_script('scripts/entitlement.sh', '--install',
                               get_option('bindir') / exe['name'],
                               install_input)
    else
      emulators += {exe['name']: emulator}
    endif

    if 'CONFIG_TRACE_SYSTEMTAP' in config_host
      foreach stp: [
        {'ext': '.stp-build', 'fmt': 'stap', 'bin': meson.current_build_dir() / exe['name'], 'install': false},
        {'ext': '.stp', 'fmt': 'stap', 'bin': get_option('prefix') / get_option('bindir') / exe['name'], 'install': true},
        {'ext': '-simpletrace.stp', 'fmt': 'simpletrace-stap', 'bin': '', 'install': true},
        {'ext': '-log.stp', 'fmt': 'log-stap', 'bin': '', 'install': true},
      ]
        custom_target(exe['name'] + stp['ext'],
                      input: trace_events_all,
                      output: exe['name'] + stp['ext'],
                      install: stp['install'],
                      install_dir: get_option('datadir') / 'systemtap/tapset',
                      command: [
                        tracetool, '--group=all', '--format=' + stp['fmt'],
                        '--binary=' + stp['bin'],
                        '--target-name=' + target_name,
                        '--target-type=' + target_type,
                        '--probe-prefix=qemu.' + target_type + '.' + target_name,
                        '@INPUT@', '@OUTPUT@'
                      ],
                      depend_files: tracetool_depends)
      endforeach
    endif
  endforeach
endforeach

# Other build targets

if 'CONFIG_PLUGIN' in config_host
  install_headers('include/qemu/qemu-plugin.h')
endif

if 'CONFIG_GUEST_AGENT' in config_host
  subdir('qga')
elif get_option('guest_agent_msi').enabled()
  error('Guest agent MSI requested, but the guest agent is not being built')
endif

# Don't build qemu-keymap if xkbcommon is not explicitly enabled
# when we don't build tools or system
if xkbcommon.found()
  # used for the update-keymaps target, so include rules even if !have_tools
  qemu_keymap = executable('qemu-keymap', files('qemu-keymap.c', 'ui/input-keymap.c') + genh,
                           dependencies: [qemuutil, xkbcommon], install: have_tools)
endif

if have_tools
  qemu_img = executable('qemu-img', [files('qemu-img.c'), hxdep],
             dependencies: [authz, block, crypto, io, qom, qemuutil], install: true)
  qemu_io = executable('qemu-io', files('qemu-io.c'),
             dependencies: [block, qemuutil], install: true)
  qemu_nbd = executable('qemu-nbd', files('qemu-nbd.c'),
               dependencies: [blockdev, qemuutil, gnutls], install: true)

  subdir('storage-daemon')
  subdir('contrib/rdmacm-mux')
  subdir('contrib/elf2dmp')

  executable('qemu-edid', files('qemu-edid.c', 'hw/display/edid-generate.c'),
             dependencies: qemuutil,
             install: true)

  if 'CONFIG_VHOST_USER' in config_host
    subdir('contrib/vhost-user-blk')
    subdir('contrib/vhost-user-gpu')
    subdir('contrib/vhost-user-input')
    subdir('contrib/vhost-user-scsi')
  endif

  if targetos == 'linux'
    executable('qemu-bridge-helper', files('qemu-bridge-helper.c'),
               dependencies: [qemuutil, libcap_ng],
               install: true,
               install_dir: get_option('libexecdir'))

    executable('qemu-pr-helper', files('scsi/qemu-pr-helper.c', 'scsi/utils.c'),
               dependencies: [authz, crypto, io, qom, qemuutil,
                              libcap_ng, mpathpersist],
               install: true)
  endif

  if have_ivshmem
    subdir('contrib/ivshmem-client')
    subdir('contrib/ivshmem-server')
  endif
endif

subdir('scripts')
subdir('tools')
subdir('pc-bios')
subdir('docs')
subdir('tests')
if gtk.found()
  subdir('po')
endif

if host_machine.system() == 'windows'
  nsis_cmd = [
    find_program('scripts/nsis.py'),
    '@OUTPUT@',
    get_option('prefix'),
    meson.current_source_dir(),
    host_machine.cpu(),
    '--',
    '-DDISPLAYVERSION=' + meson.project_version(),
  ]
  if build_docs
    nsis_cmd += '-DCONFIG_DOCUMENTATION=y'
  endif
  if gtk.found()
    nsis_cmd += '-DCONFIG_GTK=y'
  endif

  nsis = custom_target('nsis',
                       output: 'qemu-setup-' + meson.project_version() + '.exe',
                       input: files('qemu.nsi'),
                       build_always_stale: true,
                       command: nsis_cmd + ['@INPUT@'])
  alias_target('installer', nsis)
endif

#########################
# Configuration summary #
#########################

# Directories
summary_info = {}
summary_info += {'Install prefix':    get_option('prefix')}
summary_info += {'BIOS directory':    qemu_datadir}
summary_info += {'firmware path':     get_option('qemu_firmwarepath')}
summary_info += {'binary directory':  get_option('bindir')}
summary_info += {'library directory': get_option('libdir')}
summary_info += {'module directory':  qemu_moddir}
summary_info += {'libexec directory': get_option('libexecdir')}
summary_info += {'include directory': get_option('includedir')}
summary_info += {'config directory':  get_option('sysconfdir')}
if targetos != 'windows'
  summary_info += {'local state directory': get_option('localstatedir')}
  summary_info += {'Manual directory':      get_option('mandir')}
else
  summary_info += {'local state directory': 'queried at runtime'}
endif
summary_info += {'Doc directory':     get_option('docdir')}
summary_info += {'Build directory':   meson.current_build_dir()}
summary_info += {'Source path':       meson.current_source_dir()}
summary_info += {'GIT submodules':    config_host['GIT_SUBMODULES']}
summary(summary_info, bool_yn: true, section: 'Directories')

# Host binaries
summary_info = {}
summary_info += {'git':               config_host['GIT']}
summary_info += {'make':              config_host['MAKE']}
summary_info += {'python':            '@0@ (version: @1@)'.format(python.full_path(), python.language_version())}
summary_info += {'sphinx-build':      sphinx_build}
if config_host.has_key('HAVE_GDB_BIN')
  summary_info += {'gdb':             config_host['HAVE_GDB_BIN']}
endif
summary_info += {'genisoimage':       config_host['GENISOIMAGE']}
if targetos == 'windows' and config_host.has_key('CONFIG_GUEST_AGENT')
  summary_info += {'wixl':            wixl}
endif
if slirp_opt != 'disabled' and 'CONFIG_SLIRP_SMBD' in config_host
  summary_info += {'smbd':            config_host['CONFIG_SMBD_COMMAND']}
endif
summary(summary_info, bool_yn: true, section: 'Host binaries')

# Configurable features
summary_info = {}
summary_info += {'Documentation':     build_docs}
summary_info += {'system-mode emulation': have_system}
summary_info += {'user-mode emulation': have_user}
summary_info += {'block layer':       have_block}
summary_info += {'Install blobs':     get_option('install_blobs')}
summary_info += {'module support':    config_host.has_key('CONFIG_MODULES')}
if config_host.has_key('CONFIG_MODULES')
  summary_info += {'alternative module path': config_host.has_key('CONFIG_MODULE_UPGRADES')}
endif
summary_info += {'fuzzing support':   config_host.has_key('CONFIG_FUZZ')}
if have_system
  summary_info += {'Audio drivers':     config_host['CONFIG_AUDIO_DRIVERS']}
endif
summary_info += {'Trace backends':    config_host['TRACE_BACKENDS']}
if config_host['TRACE_BACKENDS'].split().contains('simple')
  summary_info += {'Trace output file': config_host['CONFIG_TRACE_FILE'] + '-<pid>'}
endif
summary_info += {'QOM debugging':     config_host.has_key('CONFIG_QOM_CAST_DEBUG')}
summary_info += {'vhost-kernel support': config_host.has_key('CONFIG_VHOST_KERNEL')}
summary_info += {'vhost-net support': config_host.has_key('CONFIG_VHOST_NET')}
summary_info += {'vhost-crypto support': config_host.has_key('CONFIG_VHOST_CRYPTO')}
summary_info += {'vhost-scsi support': config_host.has_key('CONFIG_VHOST_SCSI')}
summary_info += {'vhost-vsock support': config_host.has_key('CONFIG_VHOST_VSOCK')}
summary_info += {'vhost-user support': config_host.has_key('CONFIG_VHOST_USER')}
summary_info += {'vhost-user-blk server support': have_vhost_user_blk_server}
summary_info += {'vhost-user-fs support': config_host.has_key('CONFIG_VHOST_USER_FS')}
summary_info += {'vhost-vdpa support': config_host.has_key('CONFIG_VHOST_VDPA')}
summary_info += {'build guest agent': config_host.has_key('CONFIG_GUEST_AGENT')}
summary(summary_info, bool_yn: true, section: 'Configurable features')

# Compilation information
summary_info = {}
summary_info += {'host CPU':          cpu}
summary_info += {'host endianness':   build_machine.endian()}
summary_info += {'C compiler':        ' '.join(meson.get_compiler('c').cmd_array())}
summary_info += {'Host C compiler':   ' '.join(meson.get_compiler('c', native: true).cmd_array())}
if link_language == 'cpp'
  summary_info += {'C++ compiler':    ' '.join(meson.get_compiler('cpp').cmd_array())}
else
  summary_info += {'C++ compiler':      false}
endif
if targetos == 'darwin'
  summary_info += {'Objective-C compiler': ' '.join(meson.get_compiler('objc').cmd_array())}
endif
if targetos == 'windows'
  if 'WIN_SDK' in config_host
    summary_info += {'Windows SDK':   config_host['WIN_SDK']}
  endif
endif
summary_info += {'CFLAGS':            ' '.join(get_option('c_args')
                                               + ['-O' + get_option('optimization')]
                                               + (get_option('debug') ? ['-g'] : []))}
if link_language == 'cpp'
  summary_info += {'CXXFLAGS':        ' '.join(get_option('cpp_args')
                                               + ['-O' + get_option('optimization')]
                                               + (get_option('debug') ? ['-g'] : []))}
endif
link_args = get_option(link_language + '_link_args')
if link_args.length() > 0
  summary_info += {'LDFLAGS':         ' '.join(link_args)}
endif
summary_info += {'QEMU_CFLAGS':       config_host['QEMU_CFLAGS']}
summary_info += {'QEMU_LDFLAGS':      config_host['QEMU_LDFLAGS']}
summary_info += {'profiler':          config_host.has_key('CONFIG_PROFILER')}
summary_info += {'link-time optimization (LTO)': get_option('b_lto')}
summary_info += {'PIE':               get_option('b_pie')}
summary_info += {'static build':      config_host.has_key('CONFIG_STATIC')}
summary_info += {'malloc trim support': has_malloc_trim}
summary_info += {'membarrier':        config_host.has_key('CONFIG_MEMBARRIER')}
summary_info += {'debug stack usage': config_host.has_key('CONFIG_DEBUG_STACK_USAGE')}
summary_info += {'mutex debugging':   config_host.has_key('CONFIG_DEBUG_MUTEX')}
summary_info += {'memory allocator':  get_option('malloc')}
summary_info += {'avx2 optimization': config_host.has_key('CONFIG_AVX2_OPT')}
summary_info += {'avx512f optimization': config_host.has_key('CONFIG_AVX512F_OPT')}
summary_info += {'gprof enabled':     config_host.has_key('CONFIG_GPROF')}
summary_info += {'gcov':              get_option('b_coverage')}
summary_info += {'thread sanitizer':  config_host.has_key('CONFIG_TSAN')}
summary_info += {'CFI support':       get_option('cfi')}
if get_option('cfi')
  summary_info += {'CFI debug support': get_option('cfi_debug')}
endif
summary_info += {'strip binaries':    get_option('strip')}
summary_info += {'sparse':            sparse}
summary_info += {'mingw32 support':   targetos == 'windows'}

# snarf the cross-compilation information for tests
foreach target: target_dirs
  tcg_mak = meson.current_build_dir() / 'tests/tcg' / 'config-' + target + '.mak'
  if fs.exists(tcg_mak)
    config_cross_tcg = keyval.load(tcg_mak)
    target = config_cross_tcg['TARGET_NAME']
    compiler = ''
    if 'DOCKER_CROSS_CC_GUEST' in config_cross_tcg
      summary_info += {target + ' tests': config_cross_tcg['DOCKER_CROSS_CC_GUEST'] +
                                          ' via ' + config_cross_tcg['DOCKER_IMAGE']}
    elif 'CROSS_CC_GUEST' in config_cross_tcg
      summary_info += {target + ' tests'
                                : config_cross_tcg['CROSS_CC_GUEST'] }
    endif
   endif
endforeach

summary(summary_info, bool_yn: true, section: 'Compilation')

# Targets and accelerators
summary_info = {}
if have_system
  summary_info += {'KVM support':       config_all.has_key('CONFIG_KVM')}
  summary_info += {'HAX support':       config_all.has_key('CONFIG_HAX')}
  summary_info += {'HVF support':       config_all.has_key('CONFIG_HVF')}
  summary_info += {'WHPX support':      config_all.has_key('CONFIG_WHPX')}
  summary_info += {'NVMM support':      config_all.has_key('CONFIG_NVMM')}
  summary_info += {'Xen support':       config_host.has_key('CONFIG_XEN_BACKEND')}
  if config_host.has_key('CONFIG_XEN_BACKEND')
    summary_info += {'xen ctrl version':  config_host['CONFIG_XEN_CTRL_INTERFACE_VERSION']}
  endif
endif
summary_info += {'TCG support':       config_all.has_key('CONFIG_TCG')}
if config_all.has_key('CONFIG_TCG')
  if get_option('tcg_interpreter')
    summary_info += {'TCG backend':   'TCI (TCG with bytecode interpreter, experimental and slow)'}
  else
    summary_info += {'TCG backend':   'native (@0@)'.format(cpu)}
  endif
  summary_info += {'TCG plugins': config_host.has_key('CONFIG_PLUGIN')}
  summary_info += {'TCG debug enabled': config_host.has_key('CONFIG_DEBUG_TCG')}
endif
summary_info += {'target list':       ' '.join(target_dirs)}
if have_system
  summary_info += {'default devices':   get_option('default_devices')}
  summary_info += {'out of process emulation': multiprocess_allowed}
endif
summary(summary_info, bool_yn: true, section: 'Targets and accelerators')

# Block layer
summary_info = {}
summary_info += {'coroutine backend': config_host['CONFIG_COROUTINE_BACKEND']}
summary_info += {'coroutine pool':    config_host['CONFIG_COROUTINE_POOL'] == '1'}
if have_block
  summary_info += {'Block whitelist (rw)': config_host['CONFIG_BDRV_RW_WHITELIST']}
  summary_info += {'Block whitelist (ro)': config_host['CONFIG_BDRV_RO_WHITELIST']}
  summary_info += {'Use block whitelist in tools': config_host.has_key('CONFIG_BDRV_WHITELIST_TOOLS')}
  summary_info += {'VirtFS support':    have_virtfs}
  summary_info += {'build virtiofs daemon': have_virtiofsd}
  summary_info += {'Live block migration': config_host.has_key('CONFIG_LIVE_BLOCK_MIGRATION')}
  summary_info += {'replication support': config_host.has_key('CONFIG_REPLICATION')}
  summary_info += {'bochs support':     config_host.has_key('CONFIG_BOCHS')}
  summary_info += {'cloop support':     config_host.has_key('CONFIG_CLOOP')}
  summary_info += {'dmg support':       config_host.has_key('CONFIG_DMG')}
  summary_info += {'qcow v1 support':   config_host.has_key('CONFIG_QCOW1')}
  summary_info += {'vdi support':       config_host.has_key('CONFIG_VDI')}
  summary_info += {'vvfat support':     config_host.has_key('CONFIG_VVFAT')}
  summary_info += {'qed support':       config_host.has_key('CONFIG_QED')}
  summary_info += {'parallels support': config_host.has_key('CONFIG_PARALLELS')}
  summary_info += {'FUSE exports':      fuse}
endif
summary(summary_info, bool_yn: true, section: 'Block layer support')

# Crypto
summary_info = {}
summary_info += {'TLS priority':      config_host['CONFIG_TLS_PRIORITY']}
summary_info += {'GNUTLS support':    gnutls}
if gnutls.found()
  summary_info += {'  GNUTLS crypto':   gnutls_crypto.found()}
endif
summary_info += {'libgcrypt':         gcrypt}
summary_info += {'nettle':            nettle}
if nettle.found()
   summary_info += {'  XTS':             xts != 'private'}
endif
summary_info += {'crypto afalg':      config_host.has_key('CONFIG_AF_ALG')}
summary_info += {'rng-none':          config_host.has_key('CONFIG_RNG_NONE')}
summary_info += {'Linux keyring':     config_host.has_key('CONFIG_SECRET_KEYRING')}
summary(summary_info, bool_yn: true, section: 'Crypto')

# Libraries
summary_info = {}
if targetos == 'darwin'
  summary_info += {'Cocoa support':   cocoa}
endif
summary_info += {'SDL support':       sdl}
summary_info += {'SDL image support': sdl_image}
summary_info += {'GTK support':       gtk}
summary_info += {'pixman':            pixman}
summary_info += {'VTE support':       vte}
summary_info += {'slirp support':     slirp_opt == 'internal' ? slirp_opt : slirp}
summary_info += {'libtasn1':          tasn1}
summary_info += {'PAM':               pam}
summary_info += {'iconv support':     iconv}
summary_info += {'curses support':    curses}
summary_info += {'virgl support':     virgl}
summary_info += {'curl support':      curl}
summary_info += {'Multipath support': mpathpersist}
summary_info += {'VNC support':       vnc}
if vnc.found()
  summary_info += {'VNC SASL support':  sasl}
  summary_info += {'VNC JPEG support':  jpeg}
  summary_info += {'VNC PNG support':   png}
endif
summary_info += {'brlapi support':    brlapi}
summary_info += {'vde support':       config_host.has_key('CONFIG_VDE')}
summary_info += {'netmap support':    config_host.has_key('CONFIG_NETMAP')}
summary_info += {'Linux AIO support': config_host.has_key('CONFIG_LINUX_AIO')}
summary_info += {'Linux io_uring support': linux_io_uring}
summary_info += {'ATTR/XATTR support': libattr}
summary_info += {'RDMA support':      config_host.has_key('CONFIG_RDMA')}
summary_info += {'PVRDMA support':    config_host.has_key('CONFIG_PVRDMA')}
summary_info += {'fdt support':       fdt_opt == 'disabled' ? false : fdt_opt}
summary_info += {'libcap-ng support': libcap_ng}
summary_info += {'bpf support':       libbpf}
# TODO: add back protocol and server version
summary_info += {'spice support':     config_host.has_key('CONFIG_SPICE')}
summary_info += {'rbd support':       rbd}
summary_info += {'xfsctl support':    config_host.has_key('CONFIG_XFS')}
summary_info += {'smartcard support': cacard}
summary_info += {'U2F support':       u2f}
summary_info += {'libusb':            libusb}
summary_info += {'usb net redir':     usbredir}
summary_info += {'OpenGL support':    config_host.has_key('CONFIG_OPENGL')}
summary_info += {'GBM':               gbm}
summary_info += {'libiscsi support':  libiscsi}
summary_info += {'libnfs support':    libnfs}
if targetos == 'windows'
  if config_host.has_key('CONFIG_GUEST_AGENT')
    summary_info += {'QGA VSS support':   config_host.has_key('CONFIG_QGA_VSS')}
    summary_info += {'QGA w32 disk info': config_host.has_key('CONFIG_QGA_NTDDSCSI')}
  endif
endif
summary_info += {'seccomp support':   seccomp}
summary_info += {'GlusterFS support': glusterfs}
summary_info += {'TPM support':       config_host.has_key('CONFIG_TPM')}
summary_info += {'libssh support':    config_host.has_key('CONFIG_LIBSSH')}
summary_info += {'lzo support':       lzo}
summary_info += {'snappy support':    snappy}
summary_info += {'bzip2 support':     libbzip2}
summary_info += {'lzfse support':     liblzfse}
summary_info += {'zstd support':      zstd}
summary_info += {'NUMA host support': config_host.has_key('CONFIG_NUMA')}
summary_info += {'libxml2':           libxml2}
summary_info += {'capstone':          capstone_opt == 'internal' ? capstone_opt : capstone}
summary_info += {'libpmem support':   libpmem}
summary_info += {'libdaxctl support': libdaxctl}
summary_info += {'libudev':           libudev}
# Dummy dependency, keep .found()
summary_info += {'FUSE lseek':        fuse_lseek.found()}
summary(summary_info, bool_yn: true, section: 'Dependencies')

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
