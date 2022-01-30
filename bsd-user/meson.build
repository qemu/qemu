if not have_bsd_user
   subdir_done()
endif

bsd_user_ss = ss.source_set()

common_user_inc += include_directories('include')

bsd_user_ss.add(files(
  'bsdload.c',
  'elfload.c',
  'main.c',
  'mmap.c',
  'signal.c',
  'strace.c',
  'uaccess.c',
))

# Pull in the OS-specific build glue, if any
subdir(targetos)

specific_ss.add_all(when: 'CONFIG_BSD_USER', if_true: bsd_user_ss)
