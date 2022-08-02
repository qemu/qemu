#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include <glib.h>
#include "qpp_srv.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

void my_on_exit(int, bool);

void my_on_exit(int x, bool b)
{
  g_autoptr(GString) report = g_string_new("Client: on_exit runs with args: ");
  g_string_append_printf(report, "%d, %d\n", x, b);
  qemu_plugin_outs(report->str);

  g_string_printf(report, "Client: calls qpp_srv's do_add(1): %d\n",
                          qpp_srv_do_add(1));
  qemu_plugin_outs(report->str);

  g_string_printf(report, "Client: calls qpp_srv's do_sub(1): %d\n",
                           qpp_srv_do_sub(1));
  qemu_plugin_outs(report->str);
}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {

    /*
     * Register our "my_on_exit" function to run on the on_exit QPP-callback
     * exported by qpp_srv
     */
    QPP_REG_CB("qpp_srv", on_exit, my_on_exit);

    g_autoptr(GString) report = g_string_new(CURRENT_PLUGIN ": Call "
                                             "qpp_srv's do_add(0) => ");
    g_string_append_printf(report, "%d\n", qpp_srv_do_add(0));
    qemu_plugin_outs(report->str);

    g_string_printf(report, "Client: registered on_exit callback\n");
    return 0;
}

