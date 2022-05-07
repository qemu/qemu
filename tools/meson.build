have_virtiofsd = get_option('virtiofsd') \
    .require(targetos == 'linux',
             error_message: 'virtiofsd requires Linux') \
    .require(seccomp.found() and libcap_ng.found(),
             error_message: 'virtiofsd requires libcap-ng-devel and seccomp-devel') \
    .require(have_vhost_user,
             error_message: 'virtiofsd needs vhost-user-support') \
    .disable_auto_if(not have_tools and not have_system) \
    .allowed()

if have_virtiofsd
  subdir('virtiofsd')
endif
