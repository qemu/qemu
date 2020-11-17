have_virtiofsd = (targetos == 'linux' and
    have_tools and
    seccomp.found() and
    'CONFIG_LIBCAP_NG' in config_host and
    'CONFIG_VHOST_USER' in config_host)

if get_option('virtiofsd').enabled()
  if not have_virtiofsd
    if targetos != 'linux'
      error('virtiofsd requires Linux')
    elif not seccomp.found() or 'CONFIG_LIBCAP_NG' not in config_host
      error('virtiofsd requires libcap-ng-devel and seccomp-devel')
    elif not have_tools or 'CONFIG_VHOST_USER' not in config_host
      error('virtiofsd needs tools and vhost-user support')
    endif
  endif
elif get_option('virtiofsd').disabled() or not have_system
  have_virtiofsd = false
endif

if have_virtiofsd
  subdir('virtiofsd')
endif
