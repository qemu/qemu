if not have_user
   subdir_done()
endif

common_user_inc += include_directories('host/' / host_arch)

user_ss.add(files(
  'safe-syscall.S',
  'safe-syscall-error.c',
))
