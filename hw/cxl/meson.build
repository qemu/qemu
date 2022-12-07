system_ss.add(when: 'CONFIG_CXL',
               if_true: files(
                   'cxl-component-utils.c',
                   'cxl-device-utils.c',
                   'cxl-mailbox-utils.c',
                   'cxl-host.c',
                   'cxl-cdat.c',
                   'cxl-events.c',
# Disabled for 8.2.0 rebase for RHEL 9.4.0
#                   'switch-mailbox-cci.c',
               ),
               if_false: files(
                   'cxl-host-stubs.c',
               ))
