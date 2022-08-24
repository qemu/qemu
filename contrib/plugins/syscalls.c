#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <qemu-plugin.h>
#include <plugin-qpp.h>
#include "syscalls.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

QPP_CREATE_CB(on_all_sys_enter);
is_syscall_t is_syscall_fn = NULL;
get_callno_t get_callno_fn = NULL;

bool big_endian = false; // XXX TODO
bool is_syscall_i386(unsigned char* buf, size_t buf_len) {
  assert(buf_len >= 2);
  // Check if the instruction is syscall (0F 05)
  if (buf[0]== 0x0F && buf[1] == 0x05) {
    return true;
  }

  // Check if the instruction is int 0x80 (CD 80)
  if (buf[0]== 0xCD && buf[1] == 0x80) {
    return true;
  }

  // Check if the instruction is sysenter (0F 34)
  if (buf[0]== 0x0F && buf[1] == 0x34) {
    // If 64-bit, we want to warn and ignore this, maybe warn
    return true;
  }
  return false;
}


bool is_syscall_x86_64(unsigned char* buf, size_t buf_len) {
  assert(buf_len >= 2);
  // Check if the instruction is syscall (0F 05)
  if (buf[0]== 0x0F && buf[1] == 0x05) {
    return true;
  }

  return false;
}



bool is_syscall_arm(unsigned char* buf, size_t buf_len) {
  assert(buf_len >=4);
  // TODO THUMB MODE

  // EABI - Thumb=0
  if (((buf[3] & 0x0F) ==  0x0F)  && (buf[2] == 0) && (buf[1] == 0) && (buf[0] == 0)) {
    return true;
  }
  // OABI - Thumb=0
  if (((buf[3] & 0x0F) == 0x0F)  && (buf[2] == 0x90)) {
    //*static_callno = (buf[1]<<8) + (buf[0]);
    return true;
  }

#if 0 // IF THUMB
  if (buf[1] == 0xDF && buf[0] == 0) {
    return true;
  }
#endif
  return false;
}

bool is_syscall_aarch64(unsigned char* buf, size_t buf_len) {
  assert(buf_len >=4);
  if ((buf[0] == 0x01)  && (buf[1] == 0) && (buf[2] == 0) && (buf[3] == 0xd4)) {
    return true;
  }
  return false;
}

bool is_syscall_mips(unsigned char* buf, size_t buf_len) {
  assert(buf_len >= 4);
  if (big_endian) {
    // 32-bit MIPS "syscall" instruction - big endian
    if ((buf[0] == 0x00) && (buf[1] == 0x00) && (buf[2] == 0x00) && (buf[3] == 0x0c)) {
      return true;
    }
  } else {
    // 32-bit MIPS "syscall" instruction - little endian
    if ((buf[3] == 0x00) && (buf[2] == 0x00) && (buf[1] == 0x00) && (buf[0] == 0x0c)) {
      return true;
    }
  }
  return false;
}

bool is_syscall_other(unsigned char* buf, size_t buf_len) {
  // If we could get a handle to the insn object we could do the following:
#if 0
  char *insn_disas = qemu_plugin_insn_disas(insn);
  return (strcmp(insn_disas, "syscall ") == 0);
#endif
  return false;
}

uint64_t get_callno_i386(bool * error) {
  return (uint64_t)qemu_plugin_get_reg32(0, error);
}

uint64_t get_callno_x86_64(bool * error) {
  return qemu_plugin_get_reg64(0, error);
}

uint64_t get_callno_arm(bool * error) {
  return (uint64_t)qemu_plugin_get_reg32(7, error);
}

uint64_t get_callno_aarch64(bool * error) {
  // XXX this may be wrong, we want cpustate->env.Xregs[8] - gdbstub might not expose this
  return qemu_plugin_get_reg64(8, error);
}

uint64_t get_callno_mips(bool * error) {
  return (uint64_t)qemu_plugin_get_reg32(2, error);
}

uint64_t get_callno_other(bool * error) {
  *error=true;
  return 0;
}

typedef struct {
  const char *qemu_target;
  is_syscall_t is_syscall_fn;
  get_callno_t get_callno_fn;
} SyscallDetectorSelector;

// aarch64, sparc, sparc64, i386, x86_64
static SyscallDetectorSelector syscall_selectors[] = {
  { "i386",    is_syscall_i386,    get_callno_i386  },
  { "x86_64",  is_syscall_x86_64,  get_callno_x86_64  },
  { "arm",     is_syscall_arm,     get_callno_arm  },
  { "aarch64", is_syscall_aarch64, get_callno_aarch64  },
  { "mips",    is_syscall_mips,    get_callno_mips  },
  { NULL,      is_syscall_other,   get_callno_other  },
};

static void syscall_64(unsigned int cpu_index, void *udata) {
  bool err;
  uint64_t callno = get_callno_fn(&err);
  if (err) {
    fprintf(stderr, "Error reading register\n");
    return;
  }
  uint64_t pc = qemu_plugin_get_pc();
 QPP_RUN_CB(on_all_sys_enter, pc, callno);
}

//int first = 0;
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
  // Handle to first insns
  size_t n = qemu_plugin_tb_n_insns(tb);
  struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, n-1);
  uint32_t insn_opcode = *((uint32_t *)qemu_plugin_insn_data(insn));

  if (is_syscall_fn((unsigned char*)&insn_opcode, sizeof(uint32_t))) {
    // register syscall_64 to run before(?) the last instruction in this block
    qemu_plugin_register_vcpu_insn_exec_cb(insn, syscall_64,
        QEMU_PLUGIN_CB_R_REGS,
        NULL);
  }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                   const qemu_info_t *info, int argc, char **argv) {
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);

    //big_endian = qemu_plugin_mem_is_big_endian(info);

    for (int i = 0; i < ARRAY_SIZE(syscall_selectors); i++) {
        SyscallDetectorSelector *entry = &syscall_selectors[i];
        if (!entry->qemu_target ||
            strcmp(entry->qemu_target, info->target_name) == 0) {
            is_syscall_fn = entry->is_syscall_fn;
            get_callno_fn = entry->get_callno_fn;
            break;
        }
    }

    return 0;
}
