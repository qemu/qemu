if not have_bsd_user
   subdir_done()
endif

bsd_user_ss.add(files(
  'bsdload.c',
  'elfload.c',
  'main.c',
  'mmap.c',
  'signal.c',
  'strace.c',
  'syscall.c',
  'uaccess.c',
))

# Pull in the OS-specific build glue, if any
subdir(targetos)
