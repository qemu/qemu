#
# The main gdbstub still relies on per-build definitions of various
# types. The bits pushed to softmmu/user.c try to use guest agnostic
# types such as hwaddr.
#

specific_ss.add(files('gdbstub.c'))
softmmu_ss.add(files('softmmu.c'))
user_ss.add(files('user.c'))
