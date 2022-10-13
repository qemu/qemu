#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <gmodule.h>
#include "osi.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

QPP_CREATE_CB(on_get_current_process);
QPP_CREATE_CB(on_get_process);
QPP_CREATE_CB(on_get_current_process_handle);

OsiProc *get_current_process(void) {
    OsiProc *p = NULL;
    QPP_RUN_CB(on_get_current_process, &p);
    return p;
}

QEMU_PLUGIN_EXPORT OsiProc *get_process(const OsiProcHandle *h) {
    OsiProc *p = NULL;
    QPP_RUN_CB(on_get_process, h, &p);
    return p;
}

QEMU_PLUGIN_EXPORT OsiProcHandle *get_current_process_handle(void) {
    OsiProcHandle *h = NULL;
    QPP_RUN_CB(on_get_current_process_handle, &h);
    return h;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {
    qemu_plugin_outs("osi_stub loaded\n");

    return 0;
}
