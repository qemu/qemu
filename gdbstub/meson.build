#
# The main gdbstub still relies on per-build definitions of various
# types. The bits pushed to system/user.c try to use guest agnostic
# types such as hwaddr.
#

gdbstub_xml_files = []
foreach target : target_dirs
  config_target = config_target_mak[target]
  if 'TARGET_XML_FILES' in config_target
    foreach gdbstub_xml : config_target['TARGET_XML_FILES'].split()
      if not gdbstub_xml_files.contains('gdb-xml/' + gdbstub_xml)
        gdbstub_xml_files += 'gdb-xml/' + gdbstub_xml
      endif
    endforeach
  endif
endforeach
if gdbstub_xml_files.length() > 0
  feature_to_c = find_program('../scripts/feature_to_c.py')
  gdbstub_xml = custom_target('gdbstub-xml.c',
                              output: 'gdbstub-xml.c',
                              input: files(gdbstub_xml_files),
                              command: [feature_to_c, '@INPUT@'],
                              capture: true)
else
  gdbstub_xml = files('gdb-xml-stub.c')
endif
common_ss.add(gdbstub_xml)

# We build two versions of gdbstub, one for each mode
user_ss.add(files(
  'gdbstub.c',
  'syscalls.c',
  'user.c'
))

system_ss.add(files(
  'gdbstub.c',
  'syscalls.c',
  'system.c'
))

# The user-target is specialised by the guest
specific_ss.add(when: 'CONFIG_USER_ONLY', if_true: files('user-target.c'))
