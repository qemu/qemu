i18n = import('i18n')

if get_option('gettext')
  i18n.gettext(meson.project_name(),
               args: '--msgid-bugs-address=qemu-devel@nongnu.org',
               preset: 'glib')
endif
