project('qemu', ['c'], meson_version: '>=0.61.3',
        default_options: ['warning_level=1', 'c_std=gnu11', 'cpp_std=gnu++11', 'b_colorout=auto',
                          'b_staticpic=false', 'stdsplit=false', 'optimization=2', 'b_pie=true'],
        version: files('VERSION'))

add_test_setup('quick', exclude_suites: ['slow', 'thorough'], is_default: true)
add_test_setup('slow', exclude_suites: ['thorough'], env: ['G_TEST_SLOW=1', 'SPEED=slow'])
add_test_setup('thorough', env: ['G_TEST_SLOW=1', 'SPEED=thorough'])

meson.add_postconf_script(find_program('scripts/symlink-install-tree.py'))

not_found = dependency('', required: false)
keyval = import('keyval')
ss = import('sourceset')
fs = import('fs')

targetos = host_machine.system()
sh = find_program('sh')
config_host = keyval.load(meson.current_build_dir() / 'config-host.mak')
enable_modules = 'CONFIG_MODULES' in config_host
enable_static = 'CONFIG_STATIC' in config_host

# Allow both shared and static libraries unless --enable-static
static_kwargs = enable_static ? {'static': true} : {}

cc = meson.get_compiler('c')
all_languages = ['c']
if add_languages('cpp', required: false, native: false)
  all_languages += ['cpp']
  cxx = meson.get_compiler('cpp')
endif
if targetos == 'darwin' and \
   add_languages('objc', required: get_option('cocoa'), native: false)
  all_languages += ['objc']
  objc = meson.get_compiler('objc')
endif

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
qapi_trace_events = []

bsd_oses = ['gnu/kfreebsd', 'freebsd', 'netbsd', 'openbsd', 'dragonfly', 'darwin']
supported_oses = ['windows', 'freebsd', 'netbsd', 'openbsd', 'darwin', 'sunos', 'linux']
supported_cpus = ['ppc', 'ppc64', 's390x', 'riscv', 'x86', 'x86_64',
  'arm', 'aarch64', 'loongarch64', 'mips', 'mips64', 'sparc64']

cpu = host_machine.cpu_family()

# Unify riscv* to a single family.
if cpu in ['riscv32', 'riscv64']
  cpu = 'riscv'
endif

target_dirs = config_host['TARGET_DIRS'].split()
have_linux_user = false
have_bsd_user = false
have_system = false
foreach target : target_dirs
  have_linux_user = have_linux_user or target.endswith('linux-user')
  have_bsd_user = have_bsd_user or target.endswith('bsd-user')
  have_system = have_system or target.endswith('-softmmu')
endforeach
have_user = have_linux_user or have_bsd_user
have_tools = get_option('tools') \
  .disable_auto_if(not have_system) \
  .allowed()
have_ga = get_option('guest_agent') \
  .disable_auto_if(not have_system and not have_tools) \
  .require(targetos in ['sunos', 'linux', 'windows', 'freebsd', 'netbsd', 'openbsd'],
           error_message: 'unsupported OS for QEMU guest agent') \
  .allowed()
have_block = have_system or have_tools

python = import('python').find_installation()

if cpu not in supported_cpus
  host_arch = 'unknown'
elif cpu == 'x86'
  host_arch = 'i386'
elif cpu == 'mips64'
  host_arch = 'mips'
else
  host_arch = cpu
endif

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
elif cpu in ['riscv']
  kvm_targets = ['riscv32-softmmu', 'riscv64-softmmu']
else
  kvm_targets = []
endif

kvm_targets_c = '""'
if get_option('kvm').allowed() and targetos == 'linux'
  kvm_targets_c = '"' + '" ,"'.join(kvm_targets) + '"'
endif
config_host_data.set('CONFIG_KVM_TARGETS', kvm_targets_c)

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

dtrace = not_found
stap = not_found
if 'dtrace' in get_option('trace_backends')
  dtrace = find_program('dtrace', required: true)
  stap = find_program('stap', required: false)
  if stap.found()
    # Workaround to avoid dtrace(1) producing a file with 'hidden' symbol
    # visibility. Define STAP_SDT_V2 to produce 'default' symbol visibility
    # instead. QEMU --enable-modules depends on this because the SystemTap
    # semaphores are linked into the main binary and not the module's shared
    # object.
    add_global_arguments('-DSTAP_SDT_V2',
                         native: false, language: all_languages)
  endif
endif

if get_option('iasl') == ''
  iasl = find_program('iasl', required: false)
else
  iasl = find_program(get_option('iasl'), required: true)
endif

##################
# Compiler flags #
##################

qemu_cflags = config_host['QEMU_CFLAGS'].split()
qemu_objcflags = config_host['QEMU_OBJCFLAGS'].split()
qemu_ldflags = config_host['QEMU_LDFLAGS'].split()

if enable_static
  qemu_ldflags += get_option('b_pie') ? '-static-pie' : '-static'
endif

# Detect support for PT_GNU_RELRO + DT_BIND_NOW.
# The combination is known as "full relro", because .got.plt is read-only too.
qemu_ldflags += cc.get_supported_link_arguments('-Wl,-z,relro', '-Wl,-z,now')

if targetos == 'windows'
  qemu_ldflags += cc.get_supported_link_arguments('-Wl,--no-seh', '-Wl,--nxcompat')
  qemu_ldflags += cc.get_supported_link_arguments('-Wl,--dynamicbase', '-Wl,--high-entropy-va')
endif

if get_option('gprof')
  qemu_cflags += ['-p']
  qemu_objcflags += ['-p']
  qemu_ldflags += ['-p']
endif

# Specify linker-script with add_project_link_arguments so that it is not placed
# within a linker --start-group/--end-group pair
if get_option('fuzzing')
  # Specify a filter to only instrument code that is directly related to
  # virtual-devices.
  configure_file(output: 'instrumentation-filter',
                 input: 'scripts/oss-fuzz/instrumentation-filter-template',
                 copy: true)

  if cc.compiles('int main () { return 0; }',
                  name: '-fsanitize-coverage-allowlist=/dev/null',
                 args: ['-fsanitize-coverage-allowlist=/dev/null',
                        '-fsanitize-coverage=trace-pc'] )
    add_global_arguments('-fsanitize-coverage-allowlist=instrumentation-filter',
                         native: false, language: all_languages)
  endif

  if get_option('fuzzing_engine') == ''
    # Add CFLAGS to tell clang to add fuzzer-related instrumentation to all the
    # compiled code.  To build non-fuzzer binaries with --enable-fuzzing, link
    # everything with fsanitize=fuzzer-no-link. Otherwise, the linker will be
    # unable to bind the fuzzer-related callbacks added by instrumentation.
    add_global_arguments('-fsanitize=fuzzer-no-link',
                         native: false, language: all_languages)
    add_global_link_arguments('-fsanitize=fuzzer-no-link',
                              native: false, language: all_languages)
    # For the actual fuzzer binaries, we need to link against the libfuzzer
    # library. They need to be configurable, to support OSS-Fuzz
    fuzz_exe_ldflags = ['-fsanitize=fuzzer']
  else
    # LIB_FUZZING_ENGINE was set; assume we are running on OSS-Fuzz, and
    # the needed CFLAGS have already been provided
    fuzz_exe_ldflags = get_option('fuzzing_engine').split()
  endif
endif

# Check that the C++ compiler exists and works with the C compiler.
link_language = 'c'
linker = cc
qemu_cxxflags = []
if 'cpp' in all_languages
  add_global_arguments(['-D__STDC_LIMIT_MACROS', '-D__STDC_CONSTANT_MACROS', '-D__STDC_FORMAT_MACROS'],
                       native: false, language: 'cpp')
  foreach k: qemu_cflags
    if k not in ['-Wstrict-prototypes', '-Wmissing-prototypes', '-Wnested-externs',
                 '-Wold-style-declaration', '-Wold-style-definition', '-Wredundant-decls']
      qemu_cxxflags += [k]
    endif
  endforeach

  if cxx.links(files('scripts/main.c'), args: qemu_cflags)
    link_language = 'cpp'
    linker = cxx
  else
    message('C++ compiler does not work with C compiler')
    message('Disabling C++-specific optional code')
  endif
endif

# Exclude --warn-common with TSan to suppress warnings from the TSan libraries.
if targetos != 'sunos' and not config_host.has_key('CONFIG_TSAN')
  qemu_ldflags += linker.get_supported_link_arguments('-Wl,--warn-common')
endif

add_global_link_arguments(qemu_ldflags, native: false, language: all_languages)

add_global_arguments(qemu_cflags, native: false, language: 'c')
add_global_arguments(qemu_cxxflags, native: false, language: 'cpp')
add_global_arguments(qemu_objcflags, native: false, language: 'objc')
if targetos == 'linux'
  add_project_arguments('-isystem', meson.current_source_dir() / 'linux-headers',
                        '-isystem', 'linux-headers',
                        language: all_languages)
endif

add_project_arguments('-iquote', '.',
                      '-iquote', meson.current_source_dir(),
                      '-iquote', meson.current_source_dir() / 'include',
                      language: all_languages)

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

# Fuzzing
if get_option('fuzzing') and get_option('fuzzing_engine') == '' and \
    not cc.links('''
          #include <stdint.h>
          #include <sys/types.h>
          int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);
          int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) { return 0; }
        ''',
        args: ['-Werror', '-fsanitize=fuzzer'])
  error('Your compiler does not support -fsanitize=fuzzer')
endif

# Tracing backends
if 'ftrace' in get_option('trace_backends') and targetos != 'linux'
  error('ftrace is supported only on Linux')
endif
if 'syslog' in get_option('trace_backends') and not cc.compiles('''
    #include <syslog.h>
    int main(void) {
        openlog("qemu", LOG_PID, LOG_DAEMON);
        syslog(LOG_INFO, "configure");
        return 0;
    }''')
  error('syslog is not supported on this system')
endif

# Miscellaneous Linux-only features
get_option('mpath') \
  .require(targetos == 'linux', error_message: 'Multipath is supported only on Linux')

multiprocess_allowed = get_option('multiprocess') \
  .require(targetos == 'linux', error_message: 'Multiprocess QEMU is supported only on Linux') \
  .allowed()

vfio_user_server_allowed = get_option('vfio_user_server') \
  .require(targetos == 'linux', error_message: 'vfio-user server is supported only on Linux') \
  .allowed()

have_tpm = get_option('tpm') \
  .require(targetos != 'windows', error_message: 'TPM emulation only available on POSIX systems') \
  .allowed()

# vhost
have_vhost_user = get_option('vhost_user') \
  .disable_auto_if(targetos != 'linux') \
  .require(targetos != 'windows',
           error_message: 'vhost-user is not available on Windows').allowed()
have_vhost_vdpa = get_option('vhost_vdpa') \
  .require(targetos == 'linux',
           error_message: 'vhost-vdpa is only available on Linux').allowed()
have_vhost_kernel = get_option('vhost_kernel') \
  .require(targetos == 'linux',
           error_message: 'vhost-kernel is only available on Linux').allowed()
have_vhost_user_crypto = get_option('vhost_crypto') \
  .require(have_vhost_user,
           error_message: 'vhost-crypto requires vhost-user to be enabled').allowed()

have_vhost = have_vhost_user or have_vhost_vdpa or have_vhost_kernel

have_vhost_net_user = have_vhost_user and get_option('vhost_net').allowed()
have_vhost_net_vdpa = have_vhost_vdpa and get_option('vhost_net').allowed()
have_vhost_net_kernel = have_vhost_kernel and get_option('vhost_net').allowed()
have_vhost_net = have_vhost_net_kernel or have_vhost_net_user or have_vhost_net_vdpa

# Target-specific libraries and flags
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
midl = not_found
widl = not_found
pathcch = not_found
host_dsosuf = '.so'
if targetos == 'windows'
  midl = find_program('midl', required: false)
  widl = find_program('widl', required: false)
  pathcch = cc.find_library('pathcch')
  socket = cc.find_library('ws2_32')
  winmm = cc.find_library('winmm')

  win = import('windows')
  version_res = win.compile_resources('version.rc',
                                      depend_files: files('pc-bios/qemu-nsis.ico'),
                                      include_directories: include_directories('.'))
  host_dsosuf = '.dll'
elif targetos == 'darwin'
  coref = dependency('appleframeworks', modules: 'CoreFoundation')
  iokit = dependency('appleframeworks', modules: 'IOKit', required: false)
  host_dsosuf = '.dylib'
elif targetos == 'sunos'
  socket = [cc.find_library('socket'),
            cc.find_library('nsl'),
            cc.find_library('resolv')]
elif targetos == 'haiku'
  socket = [cc.find_library('posix_error_mapper'),
            cc.find_library('network'),
            cc.find_library('bsd')]
elif targetos == 'openbsd'
  if get_option('tcg').allowed() and target_dirs.length() > 0
    # Disable OpenBSD W^X if available
    emulator_link_args = cc.get_supported_link_arguments('-Wl,-z,wxneeded')
  endif
endif

# Target-specific configuration of accelerators
accelerators = []
if get_option('kvm').allowed() and targetos == 'linux'
  accelerators += 'CONFIG_KVM'
endif
if get_option('whpx').allowed() and targetos == 'windows'
  if get_option('whpx').enabled() and host_machine.cpu() != 'x86_64'
    error('WHPX requires 64-bit host')
  elif cc.has_header('WinHvPlatform.h', required: get_option('whpx')) and \
       cc.has_header('WinHvEmulation.h', required: get_option('whpx'))
    accelerators += 'CONFIG_WHPX'
  endif
endif
if get_option('hvf').allowed()
  hvf = dependency('appleframeworks', modules: 'Hypervisor',
                   required: get_option('hvf'))
  if hvf.found()
    accelerators += 'CONFIG_HVF'
  endif
endif
if get_option('hax').allowed()
  if get_option('hax').enabled() or targetos in ['windows', 'darwin', 'netbsd']
    accelerators += 'CONFIG_HAX'
  endif
endif
if targetos == 'netbsd'
  nvmm = cc.find_library('nvmm', required: get_option('nvmm'))
  if nvmm.found()
    accelerators += 'CONFIG_NVMM'
  endif
endif

tcg_arch = host_arch
if get_option('tcg').allowed()
  if host_arch == 'unknown'
    if get_option('tcg_interpreter')
      warning('Unsupported CPU @0@, will use TCG with TCI (slow)'.format(cpu))
    else
      error('Unsupported CPU @0@, try --enable-tcg-interpreter'.format(cpu))
    endif
  elif get_option('tcg_interpreter')
    warning('Use of the TCG interpreter is not recommended on this host')
    warning('architecture. There is a native TCG execution backend available')
    warning('which provides substantially better performance and reliability.')
    warning('It is strongly recommended to remove the --enable-tcg-interpreter')
    warning('configuration option on this architecture to use the native')
    warning('backend.')
  endif
  if get_option('tcg_interpreter')
    tcg_arch = 'tci'
    config_host += { 'CONFIG_TCG_INTERPRETER': 'y' }
  elif host_arch == 'x86_64'
    tcg_arch = 'i386'
  elif host_arch == 'ppc64'
    tcg_arch = 'ppc'
  endif
  add_project_arguments('-iquote', meson.current_source_dir() / 'tcg' / tcg_arch,
                        language: all_languages)

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

################
# Dependencies #
################

# The path to glib.h is added to all compilation commands.  This was
# grandfathered in from the QEMU Makefiles.
add_project_arguments(config_host['GLIB_CFLAGS'].split(),
                      native: false, language: all_languages)
glib = declare_dependency(compile_args: config_host['GLIB_CFLAGS'].split(),
                          link_args: config_host['GLIB_LIBS'].split(),
                          version: config_host['GLIB_VERSION'],
                          variables: {
                            'bindir': config_host['GLIB_BINDIR'],
                          })
# override glib dep with the configure results (for subprojects)
meson.override_dependency('glib-2.0', glib)
# pass down whether Glib has the slice allocator
if config_host.has_key('HAVE_GLIB_WITH_SLICE_ALLOCATOR')
  config_host_data.set('HAVE_GLIB_WITH_SLICE_ALLOCATOR', true)
endif

gio = not_found
gdbus_codegen = not_found
gdbus_codegen_error = '@0@ requires gdbus-codegen, please install libgio'
if not get_option('gio').auto() or have_system
  gio = dependency('gio-2.0', required: get_option('gio'),
                   method: 'pkg-config', kwargs: static_kwargs)
  if gio.found() and not cc.links('''
    #include <gio/gio.h>
    int main(void)
    {
      g_dbus_proxy_new_sync(0, 0, 0, 0, 0, 0, 0, 0);
      return 0;
    }''', dependencies: [glib, gio])
    if get_option('gio').enabled()
      error('The installed libgio is broken for static linking')
    endif
    gio = not_found
  endif
  if gio.found()
    gdbus_codegen = find_program(gio.get_variable('gdbus_codegen'),
                                 required: get_option('gio'))
    gio_unix = dependency('gio-unix-2.0', required: get_option('gio'),
                          method: 'pkg-config', kwargs: static_kwargs)
    gio = declare_dependency(dependencies: [gio, gio_unix],
                             version: gio.version())
  endif
endif
if gdbus_codegen.found() and get_option('cfi')
  gdbus_codegen = not_found
  gdbus_codegen_error = '@0@ uses gdbus-codegen, which does not support control flow integrity'
endif

lttng = not_found
if 'ust' in get_option('trace_backends')
  lttng = dependency('lttng-ust', required: true, version: '>= 2.1',
                     method: 'pkg-config', kwargs: static_kwargs)
endif
pixman = not_found
if have_system or have_tools
  pixman = dependency('pixman-1', required: have_system, version:'>=0.21.8',
                      method: 'pkg-config', kwargs: static_kwargs)
endif
zlib = dependency('zlib', required: true, kwargs: static_kwargs)

libaio = not_found
if not get_option('linux_aio').auto() or have_block
  libaio = cc.find_library('aio', has_headers: ['libaio.h'],
                           required: get_option('linux_aio'),
                           kwargs: static_kwargs)
endif

linux_io_uring_test = '''
  #include <liburing.h>
  #include <linux/errqueue.h>

  int main(void) { return 0; }'''

linux_io_uring = not_found
if not get_option('linux_io_uring').auto() or have_block
  linux_io_uring = dependency('liburing', version: '>=0.3',
                              required: get_option('linux_io_uring'),
                              method: 'pkg-config', kwargs: static_kwargs)
  if not cc.links(linux_io_uring_test)
    linux_io_uring = not_found
  endif
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
if get_option('attr').allowed()
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

cocoa = dependency('appleframeworks', modules: ['Cocoa', 'CoreVideo'],
                   required: get_option('cocoa'))

vmnet = dependency('appleframeworks', modules: 'vmnet', required: get_option('vmnet'))
if vmnet.found() and not cc.has_header_symbol('vmnet/vmnet.h',
                                              'VMNET_BRIDGED_MODE',
                                              dependencies: vmnet)
  vmnet = not_found
  if get_option('vmnet').enabled()
    error('vmnet.framework API is outdated')
  else
    warning('vmnet.framework API is outdated, disabling')
  endif
endif

seccomp = not_found
seccomp_has_sysrawrc = false
if not get_option('seccomp').auto() or have_system or have_tools
  seccomp = dependency('libseccomp', version: '>=2.3.0',
                       required: get_option('seccomp'),
                       method: 'pkg-config', kwargs: static_kwargs)
  if seccomp.found()
    seccomp_has_sysrawrc = cc.has_header_symbol('seccomp.h',
                                                'SCMP_FLTATR_API_SYSRAWRC',
                                                dependencies: seccomp)
  endif
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

slirp = not_found
if not get_option('slirp').auto() or have_system
  slirp = dependency('slirp', required: get_option('slirp'),
                     method: 'pkg-config', kwargs: static_kwargs)
  # slirp < 4.7 is incompatible with CFI support in QEMU.  This is because
  # it passes function pointers within libslirp as callbacks for timers.
  # When using a system-wide shared libslirp, the type information for the
  # callback is missing and the timer call produces a false positive with CFI.
  # Do not use the "version" keyword argument to produce a better error.
  # with control-flow integrity.
  if get_option('cfi') and slirp.found() and slirp.version().version_compare('<4.7')
    if get_option('slirp').enabled()
      error('Control-Flow Integrity requires libslirp 4.7.')
    else
      warning('Cannot use libslirp since Control-Flow Integrity requires libslirp >= 4.7.')
      slirp = not_found
    endif
  endif
endif

vde = not_found
if not get_option('vde').auto() or have_system or have_tools
  vde = cc.find_library('vdeplug', has_headers: ['libvdeplug.h'],
                           required: get_option('vde'),
                           kwargs: static_kwargs)
endif
if vde.found() and not cc.links('''
   #include <libvdeplug.h>
   int main(void)
   {
     struct vde_open_args a = {0, 0, 0};
     char s[] = "";
     vde_open(s, s, &a);
     return 0;
   }''', dependencies: vde)
  vde = not_found
  if get_option('cap_ng').enabled()
    error('could not link libvdeplug')
  else
    warning('could not link libvdeplug, disabling')
  endif
endif

pulse = not_found
if not get_option('pa').auto() or (targetos == 'linux' and have_system)
  pulse = dependency('libpulse', required: get_option('pa'),
                     method: 'pkg-config', kwargs: static_kwargs)
endif
alsa = not_found
if not get_option('alsa').auto() or (targetos == 'linux' and have_system)
  alsa = dependency('alsa', required: get_option('alsa'),
                    method: 'pkg-config', kwargs: static_kwargs)
endif
jack = not_found
if not get_option('jack').auto() or have_system
  jack = dependency('jack', required: get_option('jack'),
                    method: 'pkg-config', kwargs: static_kwargs)
endif
pipewire = not_found
if not get_option('pipewire').auto() or (targetos == 'linux' and have_system)
  pipewire = dependency('libpipewire-0.3', version: '>=0.3.60',
                    required: get_option('pipewire'),
                    method: 'pkg-config', kwargs: static_kwargs)
endif
sndio = not_found
if not get_option('sndio').auto() or have_system
  sndio = dependency('sndio', required: get_option('sndio'),
                    method: 'pkg-config', kwargs: static_kwargs)
endif

spice_protocol = not_found
if not get_option('spice_protocol').auto() or have_system
  spice_protocol = dependency('spice-protocol', version: '>=0.14.0',
                              required: get_option('spice_protocol'),
                              method: 'pkg-config', kwargs: static_kwargs)
endif
spice = not_found
if not get_option('spice').auto() or have_system
  spice = dependency('spice-server', version: '>=0.14.0',
                     required: get_option('spice'),
                     method: 'pkg-config', kwargs: static_kwargs)
endif
spice_headers = spice.partial_dependency(compile_args: true, includes: true)

rt = cc.find_library('rt', required: false)

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

have_vhost_user_gpu = have_tools and targetos == 'linux' and pixman.found()
if not get_option('virglrenderer').auto() or have_system or have_vhost_user_gpu
  virgl = dependency('virglrenderer',
                     method: 'pkg-config',
                     required: get_option('virglrenderer'),
                     kwargs: static_kwargs)
endif
blkio = not_found
if not get_option('blkio').auto() or have_block
  blkio = dependency('blkio',
                     method: 'pkg-config',
                     required: get_option('blkio'),
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
if targetos == 'linux' and have_tools and get_option('mpath').allowed()
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
if have_system and get_option('curses').allowed()
  curses_test = '''
    #if defined(__APPLE__) || defined(__OpenBSD__)
    #define _XOPEN_SOURCE_EXTENDED 1
    #endif
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
  curses = dependency(curses_dep_list,
                      required: false,
                      method: 'pkg-config',
                      kwargs: static_kwargs)
  msg = get_option('curses').enabled() ? 'curses library not found' : ''
  curses_compile_args = ['-DNCURSES_WIDECHAR=1']
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
  if get_option('iconv').allowed()
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
if not get_option('sdl').auto() or have_system
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
if not get_option('libssh').auto() or have_block
  libssh = dependency('libssh', version: '>=0.8.7',
                    method: 'pkg-config',
                    required: get_option('libssh'),
                    kwargs: static_kwargs)
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
if get_option('oss').allowed() and have_system
  if not cc.has_header('sys/soundcard.h')
    # not found
  elif targetos == 'netbsd'
    oss = cc.find_library('ossaudio', required: get_option('oss'),
                          kwargs: static_kwargs)
  else
    oss = declare_dependency()
  endif

  if not oss.found()
    if get_option('oss').enabled()
      error('OSS not found')
    endif
  endif
endif
dsound = not_found
if not get_option('dsound').auto() or (targetos == 'windows' and have_system)
  if cc.has_header('dsound.h')
    dsound = declare_dependency(link_args: ['-lole32', '-ldxguid'])
  endif

  if not dsound.found()
    if get_option('dsound').enabled()
      error('DirectSound not found')
    endif
  endif
endif

coreaudio = not_found
if not get_option('coreaudio').auto() or (targetos == 'darwin' and have_system)
  coreaudio = dependency('appleframeworks', modules: 'CoreAudio',
                         required: get_option('coreaudio'))
endif

opengl = not_found
if not get_option('opengl').auto() or have_system or have_vhost_user_gpu
  epoxy = dependency('epoxy', method: 'pkg-config',
                      required: get_option('opengl'), kwargs: static_kwargs)
  if cc.has_header('epoxy/egl.h', dependencies: epoxy)
    opengl = epoxy
  elif get_option('opengl').enabled()
    error('epoxy/egl.h not found')
  endif
endif
gbm = not_found
if (have_system or have_tools) and (virgl.found() or opengl.found())
  gbm = dependency('gbm', method: 'pkg-config', required: false,
                   kwargs: static_kwargs)
endif
have_vhost_user_gpu = have_vhost_user_gpu and virgl.found() and opengl.found() and gbm.found()

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
hogweed = not_found
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

gmp = dependency('gmp', required: false, method: 'pkg-config', kwargs: static_kwargs)
if nettle.found() and gmp.found()
  hogweed = dependency('hogweed', version: '>=3.4',
                       method: 'pkg-config',
                       required: get_option('nettle'),
                       kwargs: static_kwargs)
endif


gtk = not_found
gtkx11 = not_found
vte = not_found
have_gtk_clipboard = get_option('gtk_clipboard').enabled()

if not get_option('gtk').auto() or have_system
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
  elif have_gtk_clipboard
    error('GTK clipboard requested, but GTK not found')
  endif
endif

x11 = not_found
if gtkx11.found()
  x11 = dependency('x11', method: 'pkg-config', required: gtkx11.found(),
                   kwargs: static_kwargs)
endif
png = not_found
if get_option('png').allowed() and have_system
   png = dependency('libpng', version: '>=1.6.34', required: get_option('png'),
                    method: 'pkg-config', kwargs: static_kwargs)
endif
vnc = not_found
jpeg = not_found
sasl = not_found
if get_option('vnc').allowed() and have_system
  vnc = declare_dependency() # dummy dependency
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
if snappy.found() and not linker.links('''
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

numa = not_found
if not get_option('numa').auto() or have_system or have_tools
  numa = cc.find_library('numa', has_headers: ['numa.h'],
                              required: get_option('numa'),
                              kwargs: static_kwargs)
endif
if numa.found() and not cc.links('''
   #include <numa.h>
   int main(void) { return numa_available(); }
   ''', dependencies: numa)
  numa = not_found
  if get_option('numa').enabled()
    error('could not link numa')
  else
    warning('could not link numa, disabling')
  endif
endif

rdma = not_found
if not get_option('rdma').auto() or have_system
  libumad = cc.find_library('ibumad', required: get_option('rdma'))
  rdma_libs = [cc.find_library('rdmacm', has_headers: ['rdma/rdma_cma.h'],
                               required: get_option('rdma'),
                               kwargs: static_kwargs),
               cc.find_library('ibverbs', required: get_option('rdma'),
                               kwargs: static_kwargs),
               libumad]
  rdma = declare_dependency(dependencies: rdma_libs)
  foreach lib: rdma_libs
    if not lib.found()
      rdma = not_found
    endif
  endforeach
endif

xen = not_found
if get_option('xen').enabled() or (get_option('xen').auto() and have_system)
  xencontrol = dependency('xencontrol', required: false,
                          method: 'pkg-config', kwargs: static_kwargs)
  if xencontrol.found()
    xen_pc = declare_dependency(version: xencontrol.version(),
      dependencies: [
        xencontrol,
        # disabler: true makes xen_pc.found() return false if any is not found
        dependency('xenstore', required: false,
                   method: 'pkg-config', kwargs: static_kwargs,
                   disabler: true),
        dependency('xenforeignmemory', required: false,
                   method: 'pkg-config', kwargs: static_kwargs,
                   disabler: true),
        dependency('xengnttab', required: false,
                   method: 'pkg-config', kwargs: static_kwargs,
                   disabler: true),
        dependency('xenevtchn', required: false,
                   method: 'pkg-config', kwargs: static_kwargs,
                   disabler: true),
        dependency('xendevicemodel', required: false,
                   method: 'pkg-config', kwargs: static_kwargs,
                   disabler: true),
        # optional, no "disabler: true"
        dependency('xentoolcore', required: false,
                   method: 'pkg-config', kwargs: static_kwargs)])
    if xen_pc.found()
      xen = xen_pc
    endif
  endif
  if not xen.found()
    xen_tests = [ '4.11.0', '4.10.0', '4.9.0', '4.8.0', '4.7.1', '4.6.0', '4.5.0', '4.2.0' ]
    xen_libs = {
      '4.11.0': [ 'xenstore', 'xenctrl', 'xendevicemodel', 'xenforeignmemory', 'xengnttab', 'xenevtchn', 'xentoolcore' ],
      '4.10.0': [ 'xenstore', 'xenctrl', 'xendevicemodel', 'xenforeignmemory', 'xengnttab', 'xenevtchn', 'xentoolcore' ],
      '4.9.0': [ 'xenstore', 'xenctrl', 'xendevicemodel', 'xenforeignmemory', 'xengnttab', 'xenevtchn' ],
      '4.8.0': [ 'xenstore', 'xenctrl', 'xenforeignmemory', 'xengnttab', 'xenevtchn' ],
      '4.7.1': [ 'xenstore', 'xenctrl', 'xenforeignmemory', 'xengnttab', 'xenevtchn' ],
      '4.6.0': [ 'xenstore', 'xenctrl' ],
      '4.5.0': [ 'xenstore', 'xenctrl' ],
      '4.2.0': [ 'xenstore', 'xenctrl' ],
    }
    xen_deps = {}
    foreach ver: xen_tests
      # cache the various library tests to avoid polluting the logs
      xen_test_deps = []
      foreach l: xen_libs[ver]
        if l not in xen_deps
          xen_deps += { l: cc.find_library(l, required: false) }
        endif
        xen_test_deps += xen_deps[l]
      endforeach

      # Use -D to pick just one of the test programs in scripts/xen-detect.c
      xen_version = ver.split('.')
      xen_ctrl_version = xen_version[0] + \
        ('0' + xen_version[1]).substring(-2) + \
        ('0' + xen_version[2]).substring(-2)
      if cc.links(files('scripts/xen-detect.c'),
                  args: '-DCONFIG_XEN_CTRL_INTERFACE_VERSION=' + xen_ctrl_version,
                  dependencies: xen_test_deps)
        xen = declare_dependency(version: ver, dependencies: xen_test_deps)
        break
      endif
    endforeach
  endif
  if xen.found()
    accelerators += 'CONFIG_XEN'
  elif get_option('xen').enabled()
    error('could not compile and link Xen test program')
  endif
endif
have_xen_pci_passthrough = get_option('xen_pci_passthrough') \
  .require(xen.found(),
           error_message: 'Xen PCI passthrough requested but Xen not enabled') \
  .require(targetos == 'linux',
           error_message: 'Xen PCI passthrough not available on this platform') \
  .allowed()


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
canokey = not_found
if have_system
  canokey = dependency('canokey-qemu', required: get_option('canokey'),
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

# libselinux
selinux = dependency('libselinux',
                     required: get_option('selinux'),
                     method: 'pkg-config', kwargs: static_kwargs)

# Malloc tests

malloc = []
if get_option('malloc') == 'system'
  has_malloc_trim = \
    get_option('malloc_trim').allowed() and \
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

# Check whether statx() provides mount ID information

statx_mnt_id_test = gnu_source_prefix + '''
  #include <sys/stat.h>
  int main(void) {
    struct statx statxbuf;
    statx(0, "", 0, STATX_BASIC_STATS | STATX_MNT_ID, &statxbuf);
    return statxbuf.stx_mnt_id;
  }'''

has_statx_mnt_id = cc.links(statx_mnt_id_test)

have_vhost_user_blk_server = get_option('vhost_user_blk_server') \
  .require(targetos == 'linux',
           error_message: 'vhost_user_blk_server requires linux') \
  .require(have_vhost_user,
           error_message: 'vhost_user_blk_server requires vhost-user support') \
  .disable_auto_if(not have_tools and not have_system) \
  .allowed()

if get_option('fuse').disabled() and get_option('fuse_lseek').enabled()
  error('Cannot enable fuse-lseek while fuse is disabled')
endif

fuse = dependency('fuse3', required: get_option('fuse'),
                  version: '>=3.1', method: 'pkg-config',
                  kwargs: static_kwargs)

fuse_lseek = not_found
if get_option('fuse_lseek').allowed()
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

have_libvduse = (targetos == 'linux')
if get_option('libvduse').enabled()
    if targetos != 'linux'
        error('libvduse requires linux')
    endif
elif get_option('libvduse').disabled()
    have_libvduse = false
endif

have_vduse_blk_export = (have_libvduse and targetos == 'linux')
if get_option('vduse_blk_export').enabled()
    if targetos != 'linux'
        error('vduse_blk_export requires linux')
    elif not have_libvduse
        error('vduse_blk_export requires libvduse support')
    endif
elif get_option('vduse_blk_export').disabled()
    have_vduse_blk_export = false
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

# libdw
libdw = not_found
if not get_option('libdw').auto() or \
        (not enable_static and (have_system or have_user))
    libdw = dependency('libdw',
                       method: 'pkg-config',
                       kwargs: static_kwargs,
                       required: get_option('libdw'))
endif

#################
# config-host.h #
#################

audio_drivers_selected = []
if have_system
  audio_drivers_available = {
    'alsa': alsa.found(),
    'coreaudio': coreaudio.found(),
    'dsound': dsound.found(),
    'jack': jack.found(),
    'oss': oss.found(),
    'pa': pulse.found(),
    'pipewire': pipewire.found(),
    'sdl': sdl.found(),
    'sndio': sndio.found(),
  }
  foreach k, v: audio_drivers_available
    config_host_data.set('CONFIG_AUDIO_' + k.to_upper(), v)
  endforeach

  # Default to native drivers first, OSS second, SDL third
  audio_drivers_priority = \
    [ 'pa', 'coreaudio', 'dsound', 'sndio', 'oss' ] + \
    (targetos == 'linux' ? [] : [ 'sdl' ])
  audio_drivers_default = []
  foreach k: audio_drivers_priority
    if audio_drivers_available[k]
      audio_drivers_default += k
    endif
  endforeach

  foreach k: get_option('audio_drv_list')
    if k == 'default'
      audio_drivers_selected += audio_drivers_default
    elif not audio_drivers_available[k]
      error('Audio driver "@0@" not available.'.format(k))
    else
      audio_drivers_selected += k
    endif
  endforeach
endif
config_host_data.set('CONFIG_AUDIO_DRIVERS',
                     '"' + '", "'.join(audio_drivers_selected) + '", ')

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
  add_global_arguments(cfi_flags, native: false, language: all_languages)
  add_global_link_arguments(cfi_flags, native: false, language: all_languages)
endif

have_host_block_device = (targetos != 'darwin' or
    cc.has_header('IOKit/storage/IOMedia.h'))

dbus_display = get_option('dbus_display') \
  .require(gio.version().version_compare('>=2.64'),
           error_message: '-display dbus requires glib>=2.64') \
  .require(gdbus_codegen.found(),
           error_message: gdbus_codegen_error.format('-display dbus')) \
  .require(targetos != 'windows',
           error_message: '-display dbus is not available on Windows') \
  .allowed()

have_virtfs = get_option('virtfs') \
    .require(targetos == 'linux' or targetos == 'darwin',
             error_message: 'virtio-9p (virtfs) requires Linux or macOS') \
    .require(targetos == 'linux' or cc.has_function('pthread_fchdir_np'),
             error_message: 'virtio-9p (virtfs) on macOS requires the presence of pthread_fchdir_np') \
    .require(targetos == 'darwin' or libattr.found(),
             error_message: 'virtio-9p (virtfs) on Linux requires libattr-devel') \
    .disable_auto_if(not have_tools and not have_system) \
    .allowed()

have_virtfs_proxy_helper = get_option('virtfs_proxy_helper') \
    .require(targetos != 'darwin', error_message: 'the virtfs proxy helper is incompatible with macOS') \
    .require(have_virtfs, error_message: 'the virtfs proxy helper requires that virtfs is enabled') \
    .disable_auto_if(not have_tools) \
    .require(libcap_ng.found(), error_message: 'the virtfs proxy helper requires libcap-ng') \
    .allowed()

if get_option('block_drv_ro_whitelist') == ''
  config_host_data.set('CONFIG_BDRV_RO_WHITELIST', '')
else
  config_host_data.set('CONFIG_BDRV_RO_WHITELIST',
        '"' + get_option('block_drv_ro_whitelist').replace(',', '", "') + '", ')
endif
if get_option('block_drv_rw_whitelist') == ''
  config_host_data.set('CONFIG_BDRV_RW_WHITELIST', '')
else
  config_host_data.set('CONFIG_BDRV_RW_WHITELIST',
        '"' + get_option('block_drv_rw_whitelist').replace(',', '", "') + '", ')
endif

foreach k : get_option('trace_backends')
  config_host_data.set('CONFIG_TRACE_' + k.to_upper(), true)
endforeach
config_host_data.set_quoted('CONFIG_TRACE_FILE', get_option('trace_file'))
config_host_data.set_quoted('CONFIG_TLS_PRIORITY', get_option('tls_priority'))
if iasl.found()
  config_host_data.set_quoted('CONFIG_IASL', iasl.full_path())
endif
config_host_data.set_quoted('CONFIG_BINDIR', get_option('prefix') / get_option('bindir'))
config_host_data.set_quoted('CONFIG_PREFIX', get_option('prefix'))
config_host_data.set_quoted('CONFIG_QEMU_CONFDIR', get_option('prefix') / qemu_confdir)
config_host_data.set_quoted('CONFIG_QEMU_DATADIR', get_option('prefix') / qemu_datadir)
config_host_data.set_quoted('CONFIG_QEMU_DESKTOPDIR', get_option('prefix') / qemu_desktopdir)

qemu_firmwarepath = ''
foreach k : get_option('qemu_firmwarepath')
  qemu_firmwarepath += '"' + get_option('prefix') / k + '", '
endforeach
config_host_data.set('CONFIG_QEMU_FIRMWAREPATH', qemu_firmwarepath)

config_host_data.set_quoted('CONFIG_QEMU_HELPERDIR', get_option('prefix') / get_option('libexecdir'))
config_host_data.set_quoted('CONFIG_QEMU_ICONDIR', get_option('prefix') / qemu_icondir)
config_host_data.set_quoted('CONFIG_QEMU_LOCALEDIR', get_option('prefix') / get_option('localedir'))
config_host_data.set_quoted('CONFIG_QEMU_LOCALSTATEDIR', get_option('prefix') / get_option('localstatedir'))
config_host_data.set_quoted('CONFIG_QEMU_MODDIR', get_option('prefix') / qemu_moddir)
config_host_data.set_quoted('CONFIG_SYSCONFDIR', get_option('prefix') / get_option('sysconfdir'))

if config_host.has_key('CONFIG_MODULES')
  config_host_data.set('CONFIG_STAMP', run_command(
      meson.current_source_dir() / 'scripts/qemu-stamp.py',
      meson.project_version(), get_option('pkgversion'), '--',
      meson.current_source_dir() / 'configure',
      capture: true, check: true).stdout().strip())
endif

have_slirp_smbd = get_option('slirp_smbd') \
  .require(targetos != 'windows', error_message: 'Host smbd not supported on this platform.') \
  .allowed()
if have_slirp_smbd
  smbd_path = get_option('smbd')
  if smbd_path == ''
    smbd_path = (targetos == 'solaris' ? '/usr/sfw/sbin/smbd' : '/usr/sbin/smbd')
  endif
  config_host_data.set_quoted('CONFIG_SMBD_COMMAND', smbd_path)
endif

config_host_data.set('HOST_' + host_arch.to_upper(), 1)

if get_option('module_upgrades') and not enable_modules
  error('Cannot enable module-upgrades as modules are not enabled')
endif
config_host_data.set('CONFIG_MODULE_UPGRADES', get_option('module_upgrades'))

config_host_data.set('CONFIG_ATTR', libattr.found())
config_host_data.set('CONFIG_BDRV_WHITELIST_TOOLS', get_option('block_drv_whitelist_in_tools'))
config_host_data.set('CONFIG_BRLAPI', brlapi.found())
config_host_data.set('CONFIG_COCOA', cocoa.found())
config_host_data.set('CONFIG_FUZZ', get_option('fuzzing'))
config_host_data.set('CONFIG_GCOV', get_option('b_coverage'))
config_host_data.set('CONFIG_LIBUDEV', libudev.found())
config_host_data.set('CONFIG_LZO', lzo.found())
config_host_data.set('CONFIG_MPATH', mpathpersist.found())
config_host_data.set('CONFIG_MPATH_NEW_API', mpathpersist_new_api)
config_host_data.set('CONFIG_BLKIO', blkio.found())
config_host_data.set('CONFIG_CURL', curl.found())
config_host_data.set('CONFIG_CURSES', curses.found())
config_host_data.set('CONFIG_GBM', gbm.found())
config_host_data.set('CONFIG_GIO', gio.found())
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
config_host_data.set('CONFIG_GTK_CLIPBOARD', have_gtk_clipboard)
config_host_data.set('CONFIG_LIBATTR', have_old_libattr)
config_host_data.set('CONFIG_LIBCAP_NG', libcap_ng.found())
config_host_data.set('CONFIG_EBPF', libbpf.found())
config_host_data.set('CONFIG_LIBDAXCTL', libdaxctl.found())
config_host_data.set('CONFIG_LIBISCSI', libiscsi.found())
config_host_data.set('CONFIG_LIBNFS', libnfs.found())
config_host_data.set('CONFIG_LIBSSH', libssh.found())
config_host_data.set('CONFIG_LINUX_AIO', libaio.found())
config_host_data.set('CONFIG_LINUX_IO_URING', linux_io_uring.found())
config_host_data.set('CONFIG_LIBPMEM', libpmem.found())
config_host_data.set('CONFIG_NUMA', numa.found())
if numa.found()
  config_host_data.set('HAVE_NUMA_HAS_PREFERRED_MANY',
                       cc.has_function('numa_has_preferred_many',
                                       dependencies: numa))
endif
config_host_data.set('CONFIG_OPENGL', opengl.found())
config_host_data.set('CONFIG_PROFILER', get_option('profiler'))
config_host_data.set('CONFIG_RBD', rbd.found())
config_host_data.set('CONFIG_RDMA', rdma.found())
config_host_data.set('CONFIG_SDL', sdl.found())
config_host_data.set('CONFIG_SDL_IMAGE', sdl_image.found())
config_host_data.set('CONFIG_SECCOMP', seccomp.found())
if seccomp.found()
  config_host_data.set('CONFIG_SECCOMP_SYSRAWRC', seccomp_has_sysrawrc)
endif
config_host_data.set('CONFIG_SNAPPY', snappy.found())
config_host_data.set('CONFIG_TPM', have_tpm)
config_host_data.set('CONFIG_USB_LIBUSB', libusb.found())
config_host_data.set('CONFIG_VDE', vde.found())
config_host_data.set('CONFIG_VHOST_NET', have_vhost_net)
config_host_data.set('CONFIG_VHOST_NET_USER', have_vhost_net_user)
config_host_data.set('CONFIG_VHOST_NET_VDPA', have_vhost_net_vdpa)
config_host_data.set('CONFIG_VHOST_KERNEL', have_vhost_kernel)
config_host_data.set('CONFIG_VHOST_USER', have_vhost_user)
config_host_data.set('CONFIG_VHOST_CRYPTO', have_vhost_user_crypto)
config_host_data.set('CONFIG_VHOST_VDPA', have_vhost_vdpa)
config_host_data.set('CONFIG_VMNET', vmnet.found())
config_host_data.set('CONFIG_VHOST_USER_BLK_SERVER', have_vhost_user_blk_server)
config_host_data.set('CONFIG_VDUSE_BLK_EXPORT', have_vduse_blk_export)
config_host_data.set('CONFIG_PNG', png.found())
config_host_data.set('CONFIG_VNC', vnc.found())
config_host_data.set('CONFIG_VNC_JPEG', jpeg.found())
config_host_data.set('CONFIG_VNC_SASL', sasl.found())
config_host_data.set('CONFIG_VIRTFS', have_virtfs)
config_host_data.set('CONFIG_VTE', vte.found())
config_host_data.set('CONFIG_XKBCOMMON', xkbcommon.found())
config_host_data.set('CONFIG_KEYUTILS', keyutils.found())
config_host_data.set('CONFIG_GETTID', has_gettid)
config_host_data.set('CONFIG_GNUTLS', gnutls.found())
config_host_data.set('CONFIG_GNUTLS_CRYPTO', gnutls_crypto.found())
config_host_data.set('CONFIG_TASN1', tasn1.found())
config_host_data.set('CONFIG_GCRYPT', gcrypt.found())
config_host_data.set('CONFIG_NETTLE', nettle.found())
config_host_data.set('CONFIG_HOGWEED', hogweed.found())
config_host_data.set('CONFIG_QEMU_PRIVATE_XTS', xts == 'private')
config_host_data.set('CONFIG_MALLOC_TRIM', has_malloc_trim)
config_host_data.set('CONFIG_STATX', has_statx)
config_host_data.set('CONFIG_STATX_MNT_ID', has_statx_mnt_id)
config_host_data.set('CONFIG_ZSTD', zstd.found())
config_host_data.set('CONFIG_FUSE', fuse.found())
config_host_data.set('CONFIG_FUSE_LSEEK', fuse_lseek.found())
config_host_data.set('CONFIG_SPICE_PROTOCOL', spice_protocol.found())
if spice_protocol.found()
config_host_data.set('CONFIG_SPICE_PROTOCOL_MAJOR', spice_protocol.version().split('.')[0])
config_host_data.set('CONFIG_SPICE_PROTOCOL_MINOR', spice_protocol.version().split('.')[1])
config_host_data.set('CONFIG_SPICE_PROTOCOL_MICRO', spice_protocol.version().split('.')[2])
endif
config_host_data.set('CONFIG_SPICE', spice.found())
config_host_data.set('CONFIG_X11', x11.found())
config_host_data.set('CONFIG_DBUS_DISPLAY', dbus_display)
config_host_data.set('CONFIG_CFI', get_option('cfi'))
config_host_data.set('CONFIG_SELINUX', selinux.found())
config_host_data.set('CONFIG_XEN_BACKEND', xen.found())
config_host_data.set('CONFIG_LIBDW', libdw.found())
if xen.found()
  # protect from xen.version() having less than three components
  xen_version = xen.version().split('.') + ['0', '0']
  xen_ctrl_version = xen_version[0] + \
    ('0' + xen_version[1]).substring(-2) + \
    ('0' + xen_version[2]).substring(-2)
  config_host_data.set('CONFIG_XEN_CTRL_INTERFACE_VERSION', xen_ctrl_version)
endif
config_host_data.set('QEMU_VERSION', '"@0@"'.format(meson.project_version()))
config_host_data.set('QEMU_VERSION_MAJOR', meson.project_version().split('.')[0])
config_host_data.set('QEMU_VERSION_MINOR', meson.project_version().split('.')[1])
config_host_data.set('QEMU_VERSION_MICRO', meson.project_version().split('.')[2])

config_host_data.set_quoted('CONFIG_HOST_DSOSUF', host_dsosuf)
config_host_data.set('HAVE_HOST_BLOCK_DEVICE', have_host_block_device)

have_coroutine_pool = get_option('coroutine_pool')
if get_option('debug_stack_usage') and have_coroutine_pool
  message('Disabling coroutine pool to measure stack usage')
  have_coroutine_pool = false
endif
config_host_data.set10('CONFIG_COROUTINE_POOL', have_coroutine_pool)
config_host_data.set('CONFIG_DEBUG_GRAPH_LOCK', get_option('debug_graph_lock'))
config_host_data.set('CONFIG_DEBUG_MUTEX', get_option('debug_mutex'))
config_host_data.set('CONFIG_DEBUG_STACK_USAGE', get_option('debug_stack_usage'))
config_host_data.set('CONFIG_GPROF', get_option('gprof'))
config_host_data.set('CONFIG_LIVE_BLOCK_MIGRATION', get_option('live_block_migration').allowed())
config_host_data.set('CONFIG_QOM_CAST_DEBUG', get_option('qom_cast_debug'))
config_host_data.set('CONFIG_REPLICATION', get_option('replication').allowed())

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
if targetos == 'windows'
  config_host_data.set('HAVE_AFUNIX_H', cc.has_header('afunix.h'))
endif

# has_function
config_host_data.set('CONFIG_CLOSE_RANGE', cc.has_function('close_range'))
config_host_data.set('CONFIG_ACCEPT4', cc.has_function('accept4'))
config_host_data.set('CONFIG_CLOCK_ADJTIME', cc.has_function('clock_adjtime'))
config_host_data.set('CONFIG_DUP3', cc.has_function('dup3'))
config_host_data.set('CONFIG_FALLOCATE', cc.has_function('fallocate'))
config_host_data.set('CONFIG_POSIX_FALLOCATE', cc.has_function('posix_fallocate'))
# Note that we need to specify prefix: here to avoid incorrectly
# thinking that Windows has posix_memalign()
config_host_data.set('CONFIG_POSIX_MEMALIGN', cc.has_function('posix_memalign', prefix: '#include <stdlib.h>'))
config_host_data.set('CONFIG_ALIGNED_MALLOC', cc.has_function('_aligned_malloc'))
config_host_data.set('CONFIG_VALLOC', cc.has_function('valloc'))
config_host_data.set('CONFIG_MEMALIGN', cc.has_function('memalign'))
config_host_data.set('CONFIG_PPOLL', cc.has_function('ppoll'))
config_host_data.set('CONFIG_PREADV', cc.has_function('preadv', prefix: '#include <sys/uio.h>'))
config_host_data.set('CONFIG_PTHREAD_FCHDIR_NP', cc.has_function('pthread_fchdir_np'))
config_host_data.set('CONFIG_SENDFILE', cc.has_function('sendfile'))
config_host_data.set('CONFIG_SETNS', cc.has_function('setns') and cc.has_function('unshare'))
config_host_data.set('CONFIG_SYNCFS', cc.has_function('syncfs'))
config_host_data.set('CONFIG_SYNC_FILE_RANGE', cc.has_function('sync_file_range'))
config_host_data.set('CONFIG_TIMERFD', cc.has_function('timerfd_create'))
config_host_data.set('HAVE_COPY_FILE_RANGE', cc.has_function('copy_file_range'))
config_host_data.set('HAVE_GETIFADDRS', cc.has_function('getifaddrs'))
config_host_data.set('HAVE_OPENPTY', cc.has_function('openpty', dependencies: util))
config_host_data.set('HAVE_STRCHRNUL', cc.has_function('strchrnul'))
config_host_data.set('HAVE_SYSTEM_FUNCTION', cc.has_function('system', prefix: '#include <stdlib.h>'))
if rbd.found()
  config_host_data.set('HAVE_RBD_NAMESPACE_EXISTS',
                       cc.has_function('rbd_namespace_exists',
                                       dependencies: rbd,
                                       prefix: '#include <rbd/librbd.h>'))
endif
if rdma.found()
  config_host_data.set('HAVE_IBV_ADVISE_MR',
                       cc.has_function('ibv_advise_mr',
                                       dependencies: rdma,
                                       prefix: '#include <infiniband/verbs.h>'))
endif

# has_header_symbol
config_host_data.set('CONFIG_BLKZONED',
                     cc.has_header_symbol('linux/blkzoned.h', 'BLKOPENZONE'))
config_host_data.set('CONFIG_EPOLL_CREATE1',
                     cc.has_header_symbol('sys/epoll.h', 'epoll_create1'))
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
config_host_data.set('CONFIG_PRCTL_PR_SET_TIMERSLACK',
                     cc.has_header_symbol('sys/prctl.h', 'PR_SET_TIMERSLACK'))
config_host_data.set('CONFIG_RTNETLINK',
                     cc.has_header_symbol('linux/rtnetlink.h', 'IFLA_PROTO_DOWN'))
config_host_data.set('CONFIG_SYSMACROS',
                     cc.has_header_symbol('sys/sysmacros.h', 'makedev'))
config_host_data.set('HAVE_OPTRESET',
                     cc.has_header_symbol('getopt.h', 'optreset'))
config_host_data.set('HAVE_IPPROTO_MPTCP',
                     cc.has_header_symbol('netinet/in.h', 'IPPROTO_MPTCP'))

# has_member
config_host_data.set('HAVE_SIGEV_NOTIFY_THREAD_ID',
                     cc.has_member('struct sigevent', 'sigev_notify_thread_id',
                                   prefix: '#include <signal.h>'))
config_host_data.set('HAVE_STRUCT_STAT_ST_ATIM',
                     cc.has_member('struct stat', 'st_atim',
                                   prefix: '#include <sys/stat.h>'))
config_host_data.set('HAVE_BLK_ZONE_REP_CAPACITY',
                     cc.has_member('struct blk_zone', 'capacity',
                                   prefix: '#include <linux/blkzoned.h>'))

# has_type
config_host_data.set('CONFIG_IOVEC',
                     cc.has_type('struct iovec',
                                 prefix: '#include <sys/uio.h>'))
config_host_data.set('HAVE_UTMPX',
                     cc.has_type('struct utmpx',
                                 prefix: '#include <utmpx.h>'))

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

has_madvise = cc.links(gnu_source_prefix + '''
  #include <sys/types.h>
  #include <sys/mman.h>
  #include <stddef.h>
  int main(void) { return madvise(NULL, 0, MADV_DONTNEED); }''')
missing_madvise_proto = false
if has_madvise
  # Some platforms (illumos and Solaris before Solaris 11) provide madvise()
  # but forget to prototype it. In this case, has_madvise will be true (the
  # test program links despite a compile warning). To detect the
  # missing-prototype case, we try again with a definitely-bogus prototype.
  # This will only compile if the system headers don't provide the prototype;
  # otherwise the conflicting prototypes will cause a compiler error.
  missing_madvise_proto = cc.links(gnu_source_prefix + '''
    #include <sys/types.h>
    #include <sys/mman.h>
    #include <stddef.h>
    extern int madvise(int);
    int main(void) { return madvise(0); }''')
endif
config_host_data.set('CONFIG_MADVISE', has_madvise)
config_host_data.set('HAVE_MADVISE_WITHOUT_PROTOTYPE', missing_madvise_proto)

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
config_host_data.set('CONFIG_POSIX_MADVISE', cc.links(gnu_source_prefix + '''
  #include <sys/mman.h>
  #include <stddef.h>
  int main(void) { return posix_madvise(NULL, 0, POSIX_MADV_DONTNEED); }'''))

config_host_data.set('CONFIG_PTHREAD_SETNAME_NP_W_TID', cc.links(gnu_source_prefix + '''
  #include <pthread.h>

  static void *f(void *p) { return NULL; }
  int main(void)
  {
    pthread_t thread;
    pthread_create(&thread, 0, f, 0);
    pthread_setname_np(thread, "QEMU");
    return 0;
  }''', dependencies: threads))
config_host_data.set('CONFIG_PTHREAD_SETNAME_NP_WO_TID', cc.links(gnu_source_prefix + '''
  #include <pthread.h>

  static void *f(void *p) { pthread_setname_np("QEMU"); return NULL; }
  int main(void)
  {
    pthread_t thread;
    pthread_create(&thread, 0, f, 0);
    return 0;
  }''', dependencies: threads))
config_host_data.set('CONFIG_PTHREAD_SET_NAME_NP', cc.links(gnu_source_prefix + '''
  #include <pthread.h>
  #include <pthread_np.h>

  static void *f(void *p) { return NULL; }
  int main(void)
  {
    pthread_t thread;
    pthread_create(&thread, 0, f, 0);
    pthread_set_name_np(thread, "QEMU");
    return 0;
  }''', dependencies: threads))
config_host_data.set('CONFIG_PTHREAD_CONDATTR_SETCLOCK', cc.links(gnu_source_prefix + '''
  #include <pthread.h>
  #include <time.h>

  int main(void)
  {
    pthread_condattr_t attr
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    return 0;
  }''', dependencies: threads))
config_host_data.set('CONFIG_PTHREAD_AFFINITY_NP', cc.links(gnu_source_prefix + '''
  #include <pthread.h>

  static void *f(void *p) { return NULL; }
  int main(void)
  {
    int setsize = CPU_ALLOC_SIZE(64);
    pthread_t thread;
    cpu_set_t *cpuset;
    pthread_create(&thread, 0, f, 0);
    cpuset = CPU_ALLOC(64);
    CPU_ZERO_S(setsize, cpuset);
    pthread_setaffinity_np(thread, setsize, cpuset);
    pthread_getaffinity_np(thread, setsize, cpuset);
    CPU_FREE(cpuset);
    return 0;
  }''', dependencies: threads))
config_host_data.set('CONFIG_SIGNALFD', cc.links(gnu_source_prefix + '''
  #include <sys/signalfd.h>
  #include <stddef.h>
  int main(void) { return signalfd(-1, NULL, SFD_CLOEXEC); }'''))
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

config_host_data.set('HAVE_MLOCKALL', cc.links(gnu_source_prefix + '''
  #include <sys/mman.h>
  int main(void) {
    return mlockall(MCL_FUTURE);
  }'''))

have_l2tpv3 = false
if get_option('l2tpv3').allowed() and have_system
  have_l2tpv3 = cc.has_type('struct mmsghdr',
    prefix: gnu_source_prefix + '''
      #include <sys/socket.h>
      #include <linux/ip.h>''')
endif
config_host_data.set('CONFIG_L2TPV3', have_l2tpv3)

have_netmap = false
if get_option('netmap').allowed() and have_system
  have_netmap = cc.compiles('''
    #include <inttypes.h>
    #include <net/if.h>
    #include <net/netmap.h>
    #include <net/netmap_user.h>
    #if (NETMAP_API < 11) || (NETMAP_API > 15)
    #error
    #endif
    int main(void) { return 0; }''')
  if not have_netmap and get_option('netmap').enabled()
    error('Netmap headers not available')
  endif
endif
config_host_data.set('CONFIG_NETMAP', have_netmap)

# Work around a system header bug with some kernel/XFS header
# versions where they both try to define 'struct fsxattr':
# xfs headers will not try to redefine structs from linux headers
# if this macro is set.
config_host_data.set('HAVE_FSXATTR', cc.links('''
  #include <linux/fs.h>
  struct fsxattr foo;
  int main(void) {
    return 0;
  }'''))

# Some versions of Mac OS X incorrectly define SIZE_MAX
config_host_data.set('HAVE_BROKEN_SIZE_MAX', not cc.compiles('''
    #include <stdint.h>
    #include <stdio.h>
    int main(void) {
        return printf("%zu", SIZE_MAX);
    }''', args: ['-Werror']))

# See if 64-bit atomic operations are supported.
# Note that without __atomic builtins, we can only
# assume atomic loads/stores max at pointer size.
config_host_data.set('CONFIG_ATOMIC64', cc.links('''
  #include <stdint.h>
  int main(void)
  {
    uint64_t x = 0, y = 0;
    y = __atomic_load_n(&x, __ATOMIC_RELAXED);
    __atomic_store_n(&x, y, __ATOMIC_RELAXED);
    __atomic_compare_exchange_n(&x, &y, x, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    __atomic_exchange_n(&x, y, __ATOMIC_RELAXED);
    __atomic_fetch_add(&x, y, __ATOMIC_RELAXED);
    return 0;
  }'''))

has_int128 = cc.links('''
  __int128_t a;
  __uint128_t b;
  int main (void) {
    a = a + b;
    b = a * b;
    a = a * a;
    return 0;
  }''')

config_host_data.set('CONFIG_INT128', has_int128)

if has_int128
  # "do we have 128-bit atomics which are handled inline and specifically not
  # via libatomic". The reason we can't use libatomic is documented in the
  # comment starting "GCC is a house divided" in include/qemu/atomic128.h.
  # We only care about these operations on 16-byte aligned pointers, so
  # force 16-byte alignment of the pointer, which may be greater than
  # __alignof(unsigned __int128) for the host.
  atomic_test_128 = '''
    int main(int ac, char **av) {
      unsigned __int128 *p = __builtin_assume_aligned(av[ac - 1], sizeof(16));
      p[1] = __atomic_load_n(&p[0], __ATOMIC_RELAXED);
      __atomic_store_n(&p[2], p[3], __ATOMIC_RELAXED);
      __atomic_compare_exchange_n(&p[4], &p[5], p[6], 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
      return 0;
    }'''
  has_atomic128 = cc.links(atomic_test_128)

  config_host_data.set('CONFIG_ATOMIC128', has_atomic128)

  if not has_atomic128
    # Even with __builtin_assume_aligned, the above test may have failed
    # without optimization enabled.  Try again with optimizations locally
    # enabled for the function.  See
    #   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=107389
    has_atomic128_opt = cc.links('__attribute__((optimize("O1")))' + atomic_test_128)
    config_host_data.set('CONFIG_ATOMIC128_OPT', has_atomic128_opt)

    if not has_atomic128_opt
      config_host_data.set('CONFIG_CMPXCHG128', cc.links('''
        int main(void)
        {
          unsigned __int128 x = 0, y = 0;
          __sync_val_compare_and_swap_16(&x, y, x);
          return 0;
        }
      '''))
    endif
  endif
endif

config_host_data.set('CONFIG_GETAUXVAL', cc.links(gnu_source_prefix + '''
  #include <sys/auxv.h>
  int main(void) {
    return getauxval(AT_HWCAP) == 0;
  }'''))

config_host_data.set('CONFIG_USBFS', have_linux_user and cc.compiles('''
  #include <linux/usbdevice_fs.h>

  #ifndef USBDEVFS_GET_CAPABILITIES
  #error "USBDEVFS_GET_CAPABILITIES undefined"
  #endif

  #ifndef USBDEVFS_DISCONNECT_CLAIM
  #error "USBDEVFS_DISCONNECT_CLAIM undefined"
  #endif

  int main(void) { return 0; }'''))

have_keyring = get_option('keyring') \
  .require(targetos == 'linux', error_message: 'keyring is only available on Linux') \
  .require(cc.compiles('''
    #include <errno.h>
    #include <asm/unistd.h>
    #include <linux/keyctl.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    int main(void) {
        return syscall(__NR_keyctl, KEYCTL_READ, 0, NULL, NULL, 0);
    }'''), error_message: 'keyctl syscall not available on this system').allowed()
config_host_data.set('CONFIG_SECRET_KEYRING', have_keyring)

have_cpuid_h = cc.links('''
  #include <cpuid.h>
  int main(void) {
    unsigned a, b, c, d;
    unsigned max = __get_cpuid_max(0, 0);

    if (max >= 1) {
        __cpuid(1, a, b, c, d);
    }

    if (max >= 7) {
        __cpuid_count(7, 0, a, b, c, d);
    }

    return 0;
  }''')
config_host_data.set('CONFIG_CPUID_H', have_cpuid_h)

config_host_data.set('CONFIG_AVX2_OPT', get_option('avx2') \
  .require(have_cpuid_h, error_message: 'cpuid.h not available, cannot enable AVX2') \
  .require(cc.links('''
    #include <cpuid.h>
    #include <immintrin.h>
    static int __attribute__((target("avx2"))) bar(void *a) {
      __m256i x = *(__m256i *)a;
      return _mm256_testz_si256(x, x);
    }
    int main(int argc, char *argv[]) { return bar(argv[argc - 1]); }
  '''), error_message: 'AVX2 not available').allowed())

config_host_data.set('CONFIG_AVX512F_OPT', get_option('avx512f') \
  .require(have_cpuid_h, error_message: 'cpuid.h not available, cannot enable AVX512F') \
  .require(cc.links('''
    #include <cpuid.h>
    #include <immintrin.h>
    static int __attribute__((target("avx512f"))) bar(void *a) {
      __m512i x = *(__m512i *)a;
      return _mm512_test_epi64_mask(x, x);
    }
    int main(int argc, char *argv[]) { return bar(argv[argc - 1]); }
  '''), error_message: 'AVX512F not available').allowed())

config_host_data.set('CONFIG_AVX512BW_OPT', get_option('avx512bw') \
  .require(have_cpuid_h, error_message: 'cpuid.h not available, cannot enable AVX512BW') \
  .require(cc.links('''
    #include <cpuid.h>
    #include <immintrin.h>
    static int __attribute__((target("avx512bw"))) bar(void *a) {
      __m512i *x = a;
      __m512i res= _mm512_abs_epi8(*x);
      return res[1];
    }
    int main(int argc, char *argv[]) { return bar(argv[0]); }
  '''), error_message: 'AVX512BW not available').allowed())

have_pvrdma = get_option('pvrdma') \
  .require(rdma.found(), error_message: 'PVRDMA requires OpenFabrics libraries') \
  .require(cc.compiles(gnu_source_prefix + '''
    #include <sys/mman.h>
    int main(void)
    {
      char buf = 0;
      void *addr = &buf;
      addr = mremap(addr, 0, 1, MREMAP_MAYMOVE | MREMAP_FIXED);

      return 0;
    }'''), error_message: 'PVRDMA requires mremap').allowed()

if have_pvrdma
  config_host_data.set('LEGACY_RDMA_REG_MR', not cc.links('''
    #include <infiniband/verbs.h>
    int main(void)
    {
      struct ibv_mr *mr;
      struct ibv_pd *pd = NULL;
      size_t length = 10;
      uint64_t iova = 0;
      int access = 0;
      void *addr = NULL;

      mr = ibv_reg_mr_iova(pd, addr, length, iova, access);
      ibv_dereg_mr(mr);
      return 0;
    }'''))
endif

if get_option('membarrier').disabled()
  have_membarrier = false
elif targetos == 'windows'
  have_membarrier = true
elif targetos == 'linux'
  have_membarrier = cc.compiles('''
    #include <linux/membarrier.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <stdlib.h>
    int main(void) {
        syscall(__NR_membarrier, MEMBARRIER_CMD_QUERY, 0);
        syscall(__NR_membarrier, MEMBARRIER_CMD_SHARED, 0);
        exit(0);
    }''')
endif
config_host_data.set('CONFIG_MEMBARRIER', get_option('membarrier') \
  .require(have_membarrier, error_message: 'membarrier system call not available') \
  .allowed())

have_afalg = get_option('crypto_afalg') \
  .require(cc.compiles(gnu_source_prefix + '''
    #include <errno.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <linux/if_alg.h>
    int main(void) {
      int sock;
      sock = socket(AF_ALG, SOCK_SEQPACKET, 0);
      return sock;
    }
  '''), error_message: 'AF_ALG requested but could not be detected').allowed()
config_host_data.set('CONFIG_AF_ALG', have_afalg)

config_host_data.set('CONFIG_AF_VSOCK', cc.has_header_symbol(
  'linux/vm_sockets.h', 'AF_VSOCK',
  prefix: '#include <sys/socket.h>',
))

have_vss = false
have_vss_sdk = false # old xp/2003 SDK
if targetos == 'windows' and link_language == 'cpp'
  have_vss = cxx.compiles('''
    #define __MIDL_user_allocate_free_DEFINED__
    #include <vss.h>
    int main(void) { return VSS_CTX_BACKUP; }''')
  have_vss_sdk = cxx.has_header('vscoordint.h')
endif
config_host_data.set('HAVE_VSS_SDK', have_vss_sdk)

foreach k, v: config_host
  if k.startswith('CONFIG_')
    config_host_data.set(k, v == 'y' ? 1 : v)
  endif
endforeach

# Older versions of MinGW do not import _lock_file and _unlock_file properly.
# This was fixed for v6.0.0 with commit b48e3ac8969d.
if targetos == 'windows'
  config_host_data.set('HAVE__LOCK_FILE', cc.links('''
    #include <stdio.h>
    int main(void) {
      _lock_file(NULL);
      _unlock_file(NULL);
      return 0;
    }''', name: '_lock_file and _unlock_file'))
endif

if targetos == 'windows'
  mingw_has_setjmp_longjmp = cc.links('''
    #include <setjmp.h>
    int main(void) {
      /*
       * These functions are not available in setjmp header, but may be
       * available at link time, from libmingwex.a.
       */
      extern int __mingw_setjmp(jmp_buf);
      extern void __attribute__((noreturn)) __mingw_longjmp(jmp_buf, int);
      jmp_buf env;
      __mingw_setjmp(env);
      __mingw_longjmp(env, 0);
    }
  ''', name: 'mingw setjmp and longjmp')

  if cpu == 'aarch64' and not mingw_has_setjmp_longjmp
    error('mingw must provide setjmp/longjmp for windows-arm64')
  endif
endif

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
  'avr' : ['CONFIG_AVR_DIS'],
  'cris' : ['CONFIG_CRIS_DIS'],
  'hexagon' : ['CONFIG_HEXAGON_DIS'],
  'hppa' : ['CONFIG_HPPA_DIS'],
  'i386' : ['CONFIG_I386_DIS'],
  'x86_64' : ['CONFIG_I386_DIS'],
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
  'loongarch' : ['CONFIG_LOONGARCH_DIS'],
}

have_ivshmem = config_host_data.get('CONFIG_EVENTFD')
host_kconfig = \
  (get_option('fuzzing') ? ['CONFIG_FUZZ=y'] : []) + \
  (have_tpm ? ['CONFIG_TPM=y'] : []) + \
  (spice.found() ? ['CONFIG_SPICE=y'] : []) + \
  (have_ivshmem ? ['CONFIG_IVSHMEM=y'] : []) + \
  (opengl.found() ? ['CONFIG_OPENGL=y'] : []) + \
  (x11.found() ? ['CONFIG_X11=y'] : []) + \
  (have_vhost_user ? ['CONFIG_VHOST_USER=y'] : []) + \
  (have_vhost_vdpa ? ['CONFIG_VHOST_VDPA=y'] : []) + \
  (have_vhost_kernel ? ['CONFIG_VHOST_KERNEL=y'] : []) + \
  (have_virtfs ? ['CONFIG_VIRTFS=y'] : []) + \
  ('CONFIG_LINUX' in config_host ? ['CONFIG_LINUX=y'] : []) + \
  (have_pvrdma ? ['CONFIG_PVRDMA=y'] : []) + \
  (multiprocess_allowed ? ['CONFIG_MULTIPROCESS_ALLOWED=y'] : []) + \
  (vfio_user_server_allowed ? ['CONFIG_VFIO_USER_SERVER_ALLOWED=y'] : [])

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
        get_option('interp_prefix').replace('%M', config_target['TARGET_NAME'])
    }
  endif

  accel_kconfig = []
  foreach sym: accelerators
    if sym == 'CONFIG_TCG' or target in accelerator_targets.get(sym, [])
      config_target += { sym: 'y' }
      config_all += { sym: 'y' }
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
  if 'TARGET_BIG_ENDIAN' not in config_target
    config_target += {'TARGET_BIG_ENDIAN': 'n'}
  endif

  foreach k, v: disassemblers
    if host_arch.startswith(k) or config_target['TARGET_BASE_ARCH'].startswith(k)
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
    elif v == 'n'
      config_target_data.set(k, 0)
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
  'CONFIG_XEN': xen.found(),
  'CONFIG_SOFTMMU': have_system,
  'CONFIG_USER_ONLY': have_user,
  'CONFIG_ALL': true,
}

target_configs_h = []
foreach target: target_dirs
  target_configs_h += config_target_h[target]
  target_configs_h += config_devices_h.get(target, [])
endforeach
genh += custom_target('config-poison.h',
                      input: [target_configs_h],
                      output: 'config-poison.h',
                      capture: true,
                      command: [find_program('scripts/make-config-poison.sh'),
                                target_configs_h])

##############
# Submodules #
##############

capstone = not_found
if not get_option('capstone').auto() or have_system or have_user
  capstone = dependency('capstone', version: '>=3.0.5',
                        kwargs: static_kwargs, method: 'pkg-config',
                        required: get_option('capstone'))

  # Some versions of capstone have broken pkg-config file
  # that reports a wrong -I path, causing the #include to
  # fail later. If the system has such a broken version
  # do not use it.
  if capstone.found() and not cc.compiles('#include <capstone.h>',
                                          dependencies: [capstone])
    capstone = not_found
    if get_option('capstone').enabled()
      error('capstone requested, but it does not appear to work')
    endif
  endif
endif

libvfio_user_dep = not_found
if have_system and vfio_user_server_allowed
  have_internal = fs.exists(meson.current_source_dir() / 'subprojects/libvfio-user/meson.build')

  if not have_internal
    error('libvfio-user source not found - please pull git submodule')
  endif

  libvfio_user_proj = subproject('libvfio-user')

  libvfio_user_lib = libvfio_user_proj.get_variable('libvfio_user_dep')

  libvfio_user_dep = declare_dependency(dependencies: [libvfio_user_lib])
endif

fdt = not_found
if have_system
  fdt_opt = get_option('fdt')
  if fdt_opt in ['enabled', 'auto', 'system']
    have_internal = fs.exists(meson.current_source_dir() / 'dtc/libfdt/Makefile.libfdt')
    fdt = cc.find_library('fdt', kwargs: static_kwargs,
                          required: fdt_opt == 'system' or
                                    fdt_opt == 'enabled' and not have_internal)
    if fdt.found() and cc.links('''
       #include <libfdt.h>
       #include <libfdt_env.h>
       int main(void) { fdt_find_max_phandle(NULL, NULL); return 0; }''',
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
else
  fdt_opt = 'disabled'
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
shaderinclude = find_program('scripts/shaderinclude.py')
qapi_gen = find_program('scripts/qapi-gen.py')
qapi_gen_depends = [ meson.current_source_dir() / 'scripts/qapi/__init__.py',
                     meson.current_source_dir() / 'scripts/qapi/commands.py',
                     meson.current_source_dir() / 'scripts/qapi/common.py',
                     meson.current_source_dir() / 'scripts/qapi/error.py',
                     meson.current_source_dir() / 'scripts/qapi/events.py',
                     meson.current_source_dir() / 'scripts/qapi/expr.py',
                     meson.current_source_dir() / 'scripts/qapi/gen.py',
                     meson.current_source_dir() / 'scripts/qapi/introspect.py',
                     meson.current_source_dir() / 'scripts/qapi/main.py',
                     meson.current_source_dir() / 'scripts/qapi/parser.py',
                     meson.current_source_dir() / 'scripts/qapi/schema.py',
                     meson.current_source_dir() / 'scripts/qapi/source.py',
                     meson.current_source_dir() / 'scripts/qapi/types.py',
                     meson.current_source_dir() / 'scripts/qapi/visit.py',
                     meson.current_source_dir() / 'scripts/qapi-gen.py'
]

tracetool = [
  python, files('scripts/tracetool.py'),
   '--backend=' + ','.join(get_option('trace_backends'))
]
tracetool_depends = files(
  'scripts/tracetool/backend/log.py',
  'scripts/tracetool/backend/__init__.py',
  'scripts/tracetool/backend/dtrace.py',
  'scripts/tracetool/backend/ftrace.py',
  'scripts/tracetool/backend/simple.py',
  'scripts/tracetool/backend/syslog.py',
  'scripts/tracetool/backend/ust.py',
  'scripts/tracetool/format/ust_events_c.py',
  'scripts/tracetool/format/ust_events_h.py',
  'scripts/tracetool/format/__init__.py',
  'scripts/tracetool/format/d.py',
  'scripts/tracetool/format/simpletrace_stap.py',
  'scripts/tracetool/format/c.py',
  'scripts/tracetool/format/h.py',
  'scripts/tracetool/format/log_stap.py',
  'scripts/tracetool/format/stap.py',
  'scripts/tracetool/__init__.py',
  'scripts/tracetool/vcpu.py'
)

qemu_version_cmd = [find_program('scripts/qemu-version.sh'),
                    meson.current_source_dir(),
                    get_option('pkgversion'), meson.project_version()]
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
chardev_ss = ss.source_set()
common_ss = ss.source_set()
crypto_ss = ss.source_set()
hwcore_ss = ss.source_set()
io_ss = ss.source_set()
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
  'gdbstub',
]
if have_linux_user
  trace_events_subdirs += [ 'linux-user' ]
endif
if have_bsd_user
  trace_events_subdirs += [ 'bsd-user' ]
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
    'hw/hyperv',
    'hw/i2c',
    'hw/i386',
    'hw/i386/xen',
    'hw/i386/kvm',
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
    'hw/sh4',
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
    'target/nios2',
    'target/ppc',
    'target/riscv',
    'target/s390x',
    'target/s390x/kvm',
    'target/sparc',
  ]
endif

vhost_user = not_found
if targetos == 'linux' and have_vhost_user
  libvhost_user = subproject('libvhost-user')
  vhost_user = libvhost_user.get_variable('vhost_user_dep')
endif

libvduse = not_found
if have_libvduse
  libvduse_proj = subproject('libvduse')
  libvduse = libvduse_proj.get_variable('libvduse_dep')
endif

# NOTE: the trace/ subdirectory needs the qapi_trace_events variable
# that is filled in by qapi/.
subdir('qapi')
subdir('qobject')
subdir('stubs')
subdir('trace')
subdir('util')
subdir('qom')
subdir('authz')
subdir('crypto')
subdir('ui')
subdir('hw')
subdir('gdbstub')


if enable_modules
  libmodulecommon = static_library('module-common', files('module-common.c') + genh, pic: true, c_args: '-DBUILD_DSO')
  modulecommon = declare_dependency(link_whole: libmodulecommon, compile_args: '-DBUILD_DSO')
endif

qom_ss = qom_ss.apply(config_host, strict: false)
libqom = static_library('qom', qom_ss.sources() + genh,
                        dependencies: [qom_ss.dependencies()],
                        name_suffix: 'fa')
qom = declare_dependency(link_whole: libqom)

event_loop_base = files('event-loop-base.c')
event_loop_base = static_library('event-loop-base', sources: event_loop_base + genh,
                                 build_by_default: true)
event_loop_base = declare_dependency(link_whole: event_loop_base,
                                     dependencies: [qom])

stub_ss = stub_ss.apply(config_all, strict: false)

util_ss.add_all(trace_ss)
util_ss = util_ss.apply(config_all, strict: false)
libqemuutil = static_library('qemuutil',
                             sources: util_ss.sources() + stub_ss.sources() + genh,
                             dependencies: [util_ss.dependencies(), libm, threads, glib, socket, malloc, pixman])
qemuutil = declare_dependency(link_with: libqemuutil,
                              sources: genh + version_res,
                              dependencies: [event_loop_base])

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
  if config_host_data.get('CONFIG_REPLICATION')
    block_ss.add(files('replication.c'))
  endif

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
specific_ss.add(files('cpu.c'))

subdir('softmmu')

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
  pagevary = static_library('page-vary-common', sources: pagevary + genh,
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
subdir('stats')
subdir('tcg')
subdir('fpu')
subdir('accel')
subdir('plugins')
subdir('ebpf')

common_user_inc = []

subdir('common-user')
subdir('bsd-user')
subdir('linux-user')

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
  if not (d == 'block' ? have_block : have_system)
    continue
  endif

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
  foreach target : target_dirs
    if target.endswith('-softmmu')
      config_target = config_target_mak[target]
      config_devices_mak = target + '-config-devices.mak'
      modinfo_src = custom_target('modinfo-' + target + '.c',
                                  output: 'modinfo-' + target + '.c',
                                  input: modinfo_files,
                                  command: [modinfo_generate, '--devices', config_devices_mak, '@INPUT@'],
                                  capture: true)

      modinfo_lib = static_library('modinfo-' + target + '.c', modinfo_src)
      modinfo_dep = declare_dependency(link_with: modinfo_lib)

      arch = config_target['TARGET_NAME'] == 'sparc64' ? 'sparc64' : config_target['TARGET_BASE_ARCH']
      hw_arch[arch].add(modinfo_dep)
    endif
  endforeach
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
                              dependencies: [block, event_loop_base])

qmp_ss = qmp_ss.apply(config_host, strict: false)
libqmp = static_library('qmp', qmp_ss.sources() + genh,
                        dependencies: qmp_ss.dependencies(),
                        name_suffix: 'fa',
                        build_by_default: false)

qmp = declare_dependency(link_whole: [libqmp])

libchardev = static_library('chardev', chardev_ss.sources() + genh,
                            name_suffix: 'fa',
                            dependencies: chardev_ss.dependencies(),
                            build_by_default: false)

chardev = declare_dependency(link_whole: libchardev)

hwcore_ss = hwcore_ss.apply(config_host, strict: false)
libhwcore = static_library('hwcore', sources: hwcore_ss.sources() + genh,
                           name_suffix: 'fa',
                           build_by_default: false)
hwcore = declare_dependency(link_whole: libhwcore)
common_ss.add(hwcore)

###########
# Targets #
###########

emulator_modules = []
foreach m : block_mods + softmmu_mods
  emulator_modules += shared_module(m.name(),
                build_by_default: true,
                name_prefix: '',
                link_whole: m,
                install: true,
                install_dir: qemu_moddir)
endforeach
if emulator_modules.length() > 0
  alias_target('modules', emulator_modules)
endif

softmmu_ss.add(authz, blockdev, chardev, crypto, io, qmp)
common_ss.add(qom, qemuutil)

common_ss.add_all(when: 'CONFIG_SOFTMMU', if_true: [softmmu_ss])
common_ss.add_all(when: 'CONFIG_USER_ONLY', if_true: user_ss)

common_all = common_ss.apply(config_all, strict: false)
common_all = static_library('common',
                            build_by_default: false,
                            sources: common_all.sources() + genh,
                            include_directories: common_user_inc,
                            implicit_include_directories: false,
                            dependencies: common_all.dependencies(),
                            name_suffix: 'fa')

feature_to_c = find_program('scripts/feature_to_c.sh')

if targetos == 'darwin'
  entitlement = find_program('scripts/entitlement.sh')
endif

emulators = {}
foreach target : target_dirs
  config_target = config_target_mak[target]
  target_name = config_target['TARGET_NAME']
  target_base_arch = config_target['TARGET_BASE_ARCH']
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
    target_type='system'
    t = target_softmmu_arch[target_base_arch].apply(config_target, strict: false)
    arch_srcs += t.sources()
    arch_deps += t.dependencies()

    hw_dir = target_name == 'sparc64' ? 'sparc64' : target_base_arch
    hw = hw_arch[hw_dir].apply(config_target, strict: false)
    arch_srcs += hw.sources()
    arch_deps += hw.dependencies()

    arch_srcs += config_devices_h[target]
    link_args += ['@block.syms', '@qemu.syms']
  else
    abi = config_target['TARGET_ABI_DIR']
    target_type='user'
    target_inc += common_user_inc
    if target_base_arch in target_user_arch
      t = target_user_arch[target_base_arch].apply(config_target, strict: false)
      arch_srcs += t.sources()
      arch_deps += t.dependencies()
    endif
    if 'CONFIG_LINUX_USER' in config_target
      base_dir = 'linux-user'
    endif
    if 'CONFIG_BSD_USER' in config_target
      base_dir = 'bsd-user'
      target_inc += include_directories('bsd-user/' / targetos)
      target_inc += include_directories('bsd-user/host/' / host_arch)
      dir = base_dir / abi
      arch_srcs += files(dir / 'signal.c', dir / 'target_arch_cpu.c')
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

  t = target_arch[target_base_arch].apply(config_target, strict: false)
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
    if get_option('fuzzing')
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
                   command: [entitlement, '@OUTPUT@', '@INPUT@'])
      }

      meson.add_install_script(entitlement, '--install',
                               get_option('bindir') / exe['name'],
                               install_input)
    else
      emulators += {exe['name']: emulator}
    endif

    if stap.found()
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

subdir('qga')

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
               dependencies: [blockdev, qemuutil, gnutls, selinux],
               install: true)

  subdir('storage-daemon')
  subdir('contrib/rdmacm-mux')
  subdir('contrib/elf2dmp')

  executable('qemu-edid', files('qemu-edid.c', 'hw/display/edid-generate.c'),
             dependencies: qemuutil,
             install: true)

  if have_vhost_user
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
    config_host['GLIB_BINDIR'],
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
pathsep = targetos == 'windows' ? ';' : ':'
summary_info += {'firmware path':     pathsep.join(get_option('qemu_firmwarepath'))}
summary_info += {'binary directory':  get_option('prefix') / get_option('bindir')}
summary_info += {'library directory': get_option('prefix') / get_option('libdir')}
summary_info += {'module directory':  qemu_moddir}
summary_info += {'libexec directory': get_option('prefix') / get_option('libexecdir')}
summary_info += {'include directory': get_option('prefix') / get_option('includedir')}
summary_info += {'config directory':  get_option('prefix') / get_option('sysconfdir')}
if targetos != 'windows'
  summary_info += {'local state directory': get_option('prefix') / get_option('localstatedir')}
  summary_info += {'Manual directory':      get_option('prefix') / get_option('mandir')}
else
  summary_info += {'local state directory': 'queried at runtime'}
endif
summary_info += {'Doc directory':     get_option('prefix') / get_option('docdir')}
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
summary_info += {'iasl':              iasl}
summary_info += {'genisoimage':       config_host['GENISOIMAGE']}
if targetos == 'windows' and have_ga
  summary_info += {'wixl':            wixl}
endif
if slirp.found() and have_system
  summary_info += {'smbd':            have_slirp_smbd ? smbd_path : false}
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
  summary_info += {'alternative module path': get_option('module_upgrades')}
endif
summary_info += {'fuzzing support':   get_option('fuzzing')}
if have_system
  summary_info += {'Audio drivers':     ' '.join(audio_drivers_selected)}
endif
summary_info += {'Trace backends':    ','.join(get_option('trace_backends'))}
if 'simple' in get_option('trace_backends')
  summary_info += {'Trace output file': get_option('trace_file') + '-<pid>'}
endif
summary_info += {'D-Bus display':     dbus_display}
summary_info += {'QOM debugging':     get_option('qom_cast_debug')}
summary_info += {'vhost-kernel support': have_vhost_kernel}
summary_info += {'vhost-net support': have_vhost_net}
summary_info += {'vhost-user support': have_vhost_user}
summary_info += {'vhost-user-crypto support': have_vhost_user_crypto}
summary_info += {'vhost-user-blk server support': have_vhost_user_blk_server}
summary_info += {'vhost-vdpa support': have_vhost_vdpa}
summary_info += {'build guest agent': have_ga}
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
option_cflags = (get_option('debug') ? ['-g'] : [])
if get_option('optimization') != 'plain'
  option_cflags += ['-O' + get_option('optimization')]
endif
summary_info += {'CFLAGS':            ' '.join(get_option('c_args') + option_cflags)}
if link_language == 'cpp'
  summary_info += {'CXXFLAGS':        ' '.join(get_option('cpp_args') + option_cflags)}
endif
if targetos == 'darwin'
  summary_info += {'OBJCFLAGS':       ' '.join(get_option('objc_args') + option_cflags)}
endif
link_args = get_option(link_language + '_link_args')
if link_args.length() > 0
  summary_info += {'LDFLAGS':         ' '.join(link_args)}
endif
summary_info += {'QEMU_CFLAGS':       ' '.join(qemu_cflags)}
if 'cpp' in all_languages
  summary_info += {'QEMU_CXXFLAGS':     ' '.join(qemu_cxxflags)}
endif
if 'objc' in all_languages
  summary_info += {'QEMU_OBJCFLAGS':    ' '.join(qemu_objcflags)}
endif
summary_info += {'QEMU_LDFLAGS':      ' '.join(qemu_ldflags)}
summary_info += {'profiler':          get_option('profiler')}
summary_info += {'link-time optimization (LTO)': get_option('b_lto')}
summary_info += {'PIE':               get_option('b_pie')}
summary_info += {'static build':      config_host.has_key('CONFIG_STATIC')}
summary_info += {'malloc trim support': has_malloc_trim}
summary_info += {'membarrier':        have_membarrier}
summary_info += {'debug graph lock':  get_option('debug_graph_lock')}
summary_info += {'debug stack usage': get_option('debug_stack_usage')}
summary_info += {'mutex debugging':   get_option('debug_mutex')}
summary_info += {'memory allocator':  get_option('malloc')}
summary_info += {'avx2 optimization': config_host_data.get('CONFIG_AVX2_OPT')}
summary_info += {'avx512bw optimization': config_host_data.get('CONFIG_AVX512BW_OPT')}
summary_info += {'avx512f optimization': config_host_data.get('CONFIG_AVX512F_OPT')}
if get_option('gprof')
  gprof_info = 'YES (deprecated)'
else
  gprof_info = get_option('gprof')
endif
summary_info += {'gprof':             gprof_info}
summary_info += {'gcov':              get_option('b_coverage')}
summary_info += {'thread sanitizer':  config_host.has_key('CONFIG_TSAN')}
summary_info += {'CFI support':       get_option('cfi')}
if get_option('cfi')
  summary_info += {'CFI debug support': get_option('cfi_debug')}
endif
summary_info += {'strip binaries':    get_option('strip')}
summary_info += {'sparse':            sparse}
summary_info += {'mingw32 support':   targetos == 'windows'}
summary(summary_info, bool_yn: true, section: 'Compilation')

# snarf the cross-compilation information for tests
summary_info = {}
have_cross = false
foreach target: target_dirs
  tcg_mak = meson.current_build_dir() / 'tests/tcg' / target / 'config-target.mak'
  if fs.exists(tcg_mak)
    config_cross_tcg = keyval.load(tcg_mak)
    if 'CC' in config_cross_tcg
      summary_info += {config_cross_tcg['TARGET_NAME']: config_cross_tcg['CC']}
      have_cross = true
    endif
  endif
endforeach
if have_cross
  summary(summary_info, bool_yn: true, section: 'Cross compilers')
endif

# Targets and accelerators
summary_info = {}
if have_system
  summary_info += {'KVM support':       config_all.has_key('CONFIG_KVM')}
  summary_info += {'HAX support':       config_all.has_key('CONFIG_HAX')}
  summary_info += {'HVF support':       config_all.has_key('CONFIG_HVF')}
  summary_info += {'WHPX support':      config_all.has_key('CONFIG_WHPX')}
  summary_info += {'NVMM support':      config_all.has_key('CONFIG_NVMM')}
  summary_info += {'Xen support':       xen.found()}
  if xen.found()
    summary_info += {'xen ctrl version':  xen.version()}
  endif
  summary_info += {'Xen emulation':     config_all.has_key('CONFIG_XEN_EMU')}
endif
summary_info += {'TCG support':       config_all.has_key('CONFIG_TCG')}
if config_all.has_key('CONFIG_TCG')
  if get_option('tcg_interpreter')
    summary_info += {'TCG backend':   'TCI (TCG with bytecode interpreter, slow)'}
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
  summary_info += {'vfio-user server': vfio_user_server_allowed}
endif
summary(summary_info, bool_yn: true, section: 'Targets and accelerators')

# Block layer
summary_info = {}
summary_info += {'coroutine backend': config_host['CONFIG_COROUTINE_BACKEND']}
summary_info += {'coroutine pool':    have_coroutine_pool}
if have_block
  summary_info += {'Block whitelist (rw)': get_option('block_drv_rw_whitelist')}
  summary_info += {'Block whitelist (ro)': get_option('block_drv_ro_whitelist')}
  summary_info += {'Use block whitelist in tools': get_option('block_drv_whitelist_in_tools')}
  summary_info += {'VirtFS (9P) support':    have_virtfs}
  summary_info += {'VirtFS (9P) Proxy Helper support': have_virtfs_proxy_helper}
  summary_info += {'Live block migration': config_host_data.get('CONFIG_LIVE_BLOCK_MIGRATION')}
  summary_info += {'replication support': config_host_data.get('CONFIG_REPLICATION')}
  summary_info += {'bochs support':     get_option('bochs').allowed()}
  summary_info += {'cloop support':     get_option('cloop').allowed()}
  summary_info += {'dmg support':       get_option('dmg').allowed()}
  summary_info += {'qcow v1 support':   get_option('qcow1').allowed()}
  summary_info += {'vdi support':       get_option('vdi').allowed()}
  summary_info += {'vhdx support':      get_option('vhdx').allowed()}
  summary_info += {'vmdk support':      get_option('vmdk').allowed()}
  summary_info += {'vpc support':       get_option('vpc').allowed()}
  summary_info += {'vvfat support':     get_option('vvfat').allowed()}
  summary_info += {'qed support':       get_option('qed').allowed()}
  summary_info += {'parallels support': get_option('parallels').allowed()}
  summary_info += {'FUSE exports':      fuse}
  summary_info += {'VDUSE block exports': have_vduse_blk_export}
endif
summary(summary_info, bool_yn: true, section: 'Block layer support')

# Crypto
summary_info = {}
summary_info += {'TLS priority':      get_option('tls_priority')}
summary_info += {'GNUTLS support':    gnutls}
if gnutls.found()
  summary_info += {'  GNUTLS crypto':   gnutls_crypto.found()}
endif
summary_info += {'libgcrypt':         gcrypt}
summary_info += {'nettle':            nettle}
if nettle.found()
   summary_info += {'  XTS':             xts != 'private'}
endif
summary_info += {'AF_ALG support':    have_afalg}
summary_info += {'rng-none':          get_option('rng_none')}
summary_info += {'Linux keyring':     have_keyring}
summary(summary_info, bool_yn: true, section: 'Crypto')

# Libraries
summary_info = {}
if targetos == 'darwin'
  summary_info += {'Cocoa support':           cocoa}
  summary_info += {'vmnet.framework support': vmnet}
endif
summary_info += {'SDL support':       sdl}
summary_info += {'SDL image support': sdl_image}
summary_info += {'GTK support':       gtk}
summary_info += {'pixman':            pixman}
summary_info += {'VTE support':       vte}
summary_info += {'slirp support':     slirp}
summary_info += {'libtasn1':          tasn1}
summary_info += {'PAM':               pam}
summary_info += {'iconv support':     iconv}
summary_info += {'curses support':    curses}
summary_info += {'virgl support':     virgl}
summary_info += {'blkio support':     blkio}
summary_info += {'curl support':      curl}
summary_info += {'Multipath support': mpathpersist}
summary_info += {'PNG support':       png}
summary_info += {'VNC support':       vnc}
if vnc.found()
  summary_info += {'VNC SASL support':  sasl}
  summary_info += {'VNC JPEG support':  jpeg}
endif
if targetos not in ['darwin', 'haiku', 'windows']
  summary_info += {'OSS support':     oss}
  summary_info += {'sndio support':   sndio}
elif targetos == 'darwin'
  summary_info += {'CoreAudio support': coreaudio}
elif targetos == 'windows'
  summary_info += {'DirectSound support': dsound}
endif
if targetos == 'linux'
  summary_info += {'ALSA support':    alsa}
  summary_info += {'PulseAudio support': pulse}
endif
summary_info += {'Pipewire support':   pipewire}
summary_info += {'JACK support':      jack}
summary_info += {'brlapi support':    brlapi}
summary_info += {'vde support':       vde}
summary_info += {'netmap support':    have_netmap}
summary_info += {'l2tpv3 support':    have_l2tpv3}
summary_info += {'Linux AIO support': libaio}
summary_info += {'Linux io_uring support': linux_io_uring}
summary_info += {'ATTR/XATTR support': libattr}
summary_info += {'RDMA support':      rdma}
summary_info += {'PVRDMA support':    have_pvrdma}
summary_info += {'fdt support':       fdt_opt == 'disabled' ? false : fdt_opt}
summary_info += {'libcap-ng support': libcap_ng}
summary_info += {'bpf support':       libbpf}
summary_info += {'spice protocol support': spice_protocol}
if spice_protocol.found()
  summary_info += {'  spice server support': spice}
endif
summary_info += {'rbd support':       rbd}
summary_info += {'smartcard support': cacard}
summary_info += {'U2F support':       u2f}
summary_info += {'libusb':            libusb}
summary_info += {'usb net redir':     usbredir}
summary_info += {'OpenGL support (epoxy)': opengl}
summary_info += {'GBM':               gbm}
summary_info += {'libiscsi support':  libiscsi}
summary_info += {'libnfs support':    libnfs}
if targetos == 'windows'
  if have_ga
    summary_info += {'QGA VSS support':   have_qga_vss}
  endif
endif
summary_info += {'seccomp support':   seccomp}
summary_info += {'GlusterFS support': glusterfs}
summary_info += {'TPM support':       have_tpm}
summary_info += {'libssh support':    libssh}
summary_info += {'lzo support':       lzo}
summary_info += {'snappy support':    snappy}
summary_info += {'bzip2 support':     libbzip2}
summary_info += {'lzfse support':     liblzfse}
summary_info += {'zstd support':      zstd}
summary_info += {'NUMA host support': numa}
summary_info += {'capstone':          capstone}
summary_info += {'libpmem support':   libpmem}
summary_info += {'libdaxctl support': libdaxctl}
summary_info += {'libudev':           libudev}
# Dummy dependency, keep .found()
summary_info += {'FUSE lseek':        fuse_lseek.found()}
summary_info += {'selinux':           selinux}
summary_info += {'libdw':             libdw}
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
