/**
 * This is a dummy header to give info to PyPANDA
 * 
 */

#define __attribute__(s)
#define __restrict
#define asm(...)
#define G_GNUC_PRINTF(...)
#define ULONG_MAX UINT64_MAX
#include <stdint.h>
void qemu_init(int argc, char **argv);
int qemu_main_loop(void);
void qemu_cleanup(void);
extern int qemu_loglevel;



// #include <stdarg.h>
// #include <limits.h>
// #include <glib.h>
// #include "qemu/typedefs.h"
// #include "qemu/bitops.h"
// #include "qemu/notify.h"
// #include "qemu/host-utils.h"
// #include "sysemu/sysemu.h"
#include "qemu/qemu-plugin.h"
extern int (*external_plugin_install)(qemu_plugin_id_t id, const qemu_info_t *info,int argc, char **argv);
