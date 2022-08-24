#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include "syscalls.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

void log_syscall(uint64_t pc, uint64_t callno);

void log_syscall(uint64_t pc, uint64_t callno)
{
  g_autoptr(GString) report = g_string_new(CURRENT_PLUGIN ": Syscall at ");
  g_string_append_printf(report, "%lx: %ld\n", pc, callno);
  qemu_plugin_outs(report->str);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv)
{
  QPP_REG_CB("syscalls", on_all_sys_enter, log_syscall)
  return 0;
}
