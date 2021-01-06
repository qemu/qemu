i18n = import('i18n')

if find_program('xgettext', required: get_option('gettext')).found()
  i18n.gettext(meson.project_name(),
               args: '--msgid-bugs-address=qemu-devel@nongnu.org',
               preset: 'glib')
endif
