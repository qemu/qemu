system_ss.add(when: 'CONFIG_CXL',
               if_true: files(
                   'cxl-component-utils.c',
                   'cxl-device-utils.c',
                   'cxl-mailbox-utils.c',
                   'cxl-host.c',
                   'cxl-cdat.c',
                   'cxl-events.c',
                   'switch-mailbox-cci.c',
               ),
               if_false: files(
                   'cxl-host-stubs.c',
               ))
