#
# The main gdbstub still relies on per-build definitions of various
# types. The bits pushed to softmmu/user.c try to use guest agnostic
# types such as hwaddr.
#

# We need to build the core gdb code via a library to be able to tweak
# cflags so:

gdb_user_ss = ss.source_set()
gdb_system_ss = ss.source_set()

# We build two versions of gdbstub, one for each mode
gdb_user_ss.add(files('gdbstub.c', 'user.c'))
gdb_system_ss.add(files('gdbstub.c', 'softmmu.c'))

gdb_user_ss = gdb_user_ss.apply(config_host, strict: false)
gdb_system_ss = gdb_system_ss.apply(config_host, strict: false)

libgdb_user = static_library('gdb_user',
                             gdb_user_ss.sources() + genh,
                             name_suffix: 'fa',
                             c_args: '-DCONFIG_USER_ONLY',
                             build_by_default: have_user)

libgdb_softmmu = static_library('gdb_softmmu',
                                gdb_system_ss.sources() + genh,
                                name_suffix: 'fa',
                                build_by_default: have_system)

gdb_user = declare_dependency(link_whole: libgdb_user)
user_ss.add(gdb_user)
gdb_softmmu = declare_dependency(link_whole: libgdb_softmmu)
system_ss.add(gdb_softmmu)

common_ss.add(files('syscalls.c'))

# The user-target is specialised by the guest
specific_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-target.c'))
