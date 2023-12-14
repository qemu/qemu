#ifndef VSS_HANDLES_H
#define VSS_HANDLES_H

/* Constants for QGA VSS Provider */

#define QGA_PROVIDER_NAME "QEMU Guest Agent VSS Provider"
#define QGA_PROVIDER_LNAME L(QGA_PROVIDER_NAME)
#define QGA_PROVIDER_VERSION L(QEMU_VERSION)
#define QGA_PROVIDER_REGISTRY_ADDRESS "SYSTEM\\CurrentControlSet"\
                                      "\\Services"\
                                      "\\" QGA_PROVIDER_NAME

#define EVENT_NAME_FROZEN  "Global\\QGAVSSEvent-frozen"
#define EVENT_NAME_THAW    "Global\\QGAVSSEvent-thaw"
#define EVENT_NAME_TIMEOUT "Global\\QGAVSSEvent-timeout"

#endif
