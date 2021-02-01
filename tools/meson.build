have_virtiofsd = (targetos == 'linux' and
    have_tools and
    seccomp.found() and
    libcap_ng.found() and
    'CONFIG_VHOST_USER' in config_host)

if get_option('virtiofsd').enabled()
  if not have_virtiofsd
    if targetos != 'linux'
      error('virtiofsd requires Linux')
    elif not seccomp.found() or not libcap_ng.found()
      error('virtiofsd requires libcap-ng-devel and seccomp-devel')
    elif 'CONFIG_VHOST_USER' not in config_host
      error('virtiofsd needs vhost-user support')
    else
      # Disabled all the tools but virtiofsd.
      have_virtiofsd = true
    endif
  endif
elif get_option('virtiofsd').disabled() or not have_system
  have_virtiofsd = false
endif

if have_virtiofsd
  subdir('virtiofsd')
endif
