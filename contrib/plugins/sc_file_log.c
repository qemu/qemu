#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include "syscalls.h"
#include "sc_file_log.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

get_syscall_arg_t get_syscall_arg = NULL;
should_log_t should_log = NULL;

typedef struct {
  const char *qemu_target;
  get_syscall_arg_t get_syscall_arg_f;
  should_log_t should_log_f;
} SyscallArgSelector;

uint64_t get_i386(int arg_no, bool *error) {
  int reg_ids[] = {3, 1, 2};
  if (arg_no > ARRAY_SIZE(reg_ids)) {
    *error = true;
    return 0;
  }
  return (uint64_t)(qemu_plugin_get_reg32(reg_ids[arg_no], error) & 0xffffffff);
}

uint64_t get_x86_64(int arg_no, bool *error) {
  int reg_ids[] = {5, 4, 3, 10, 8, 9}; // RDI, RSI, RDX, R10, R8, R9
  if (arg_no > ARRAY_SIZE(reg_ids)) {
    *error = true;
    return 0;
  }
  return qemu_plugin_get_reg64(reg_ids[arg_no], error);
}

uint64_t get_arm(int arg_no, bool *error) {
  int reg_ids[] = {0, 1, 2, 3};
  if (arg_no > ARRAY_SIZE(reg_ids)) {
    *error = true;
    return 0;
  }
  return (uint64_t)(qemu_plugin_get_reg32(reg_ids[arg_no], error) & 0xffffffff);
}

uint64_t get_other (int arg_no, bool*error) {
    *error = true;
    fprintf(stderr, "Architecture unsupported by " CURRENT_PLUGIN "\n");
    assert(0);
    return 0;
}

bool should_log_i386(int arg_no) {
  return arg_no == 5 || arg_no == 11; // open or execve
}

bool should_log_x86_64(int arg_no) {
  return arg_no == 2 || arg_no == 59;
}

bool should_log_arm(int arg_no) {
  return (arg_no == 5 || arg_no == 11);
}

bool should_log_other(int arg_no) {
  return false;
}


// aarch64, sparc, sparc64, i386, x86_64
static SyscallArgSelector arg_selectors[] = {
  { "i386",    get_i386,    should_log_i386},
  { "x86_64",  get_x86_64,  should_log_x86_64},
  { "arm",     get_arm,     should_log_arm},
  //{ "aarch64", get_aarch64},
  //{ "mips",    get_mips},
  { NULL,      get_other,   should_log_other},
};

void log_syscall(uint64_t pc, uint64_t callno)
{
  if (should_log(callno)) {
    bool error = false;
    char str_val[100];
    uint64_t str_ptr = get_syscall_arg(0, &error);

    if (!error) {
      g_autoptr(GString) report = g_string_new(CURRENT_PLUGIN ": ");
      if (qemu_plugin_read_guest_virt_mem(str_ptr, str_val, 100)) {
        g_string_append_printf(report, "Syscall %ld: %s\n", callno, str_val);
        qemu_plugin_outs(report->str);
      }
    }
  }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv)
{

  // Select the aproperiate syscall arg getter func for this arch
  for (int i = 0; i < ARRAY_SIZE(arg_selectors); i++) {
      SyscallArgSelector *entry = &arg_selectors[i];
      if (!entry->qemu_target ||
          strcmp(entry->qemu_target, info->target_name) == 0) {
          get_syscall_arg = entry->get_syscall_arg_f;
          should_log = entry->should_log_f;
          break;
      }
  }

  QPP_REG_CB("syscalls", on_all_sys_enter, log_syscall)
  return 0;
}
