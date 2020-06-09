/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_PLUGIN_API_H
#define QEMU_PLUGIN_API_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * For best performance, build the plugin with -fvisibility=hidden so that
 * QEMU_PLUGIN_LOCAL is implicit. Then, just mark qemu_plugin_install with
 * QEMU_PLUGIN_EXPORT. For more info, see
 *   https://gcc.gnu.org/wiki/Visibility
 */
#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_DLL
    #define QEMU_PLUGIN_EXPORT __declspec(dllexport)
  #else
    #define QEMU_PLUGIN_EXPORT __declspec(dllimport)
  #endif
  #define QEMU_PLUGIN_LOCAL
#else
  #if __GNUC__ >= 4
    #define QEMU_PLUGIN_EXPORT __attribute__((visibility("default")))
    #define QEMU_PLUGIN_LOCAL  __attribute__((visibility("hidden")))
  #else
    #define QEMU_PLUGIN_EXPORT
    #define QEMU_PLUGIN_LOCAL
  #endif
#endif

typedef uint64_t qemu_plugin_id_t;

/*
 * Versioning plugins:
 *
 * The plugin API will pass a minimum and current API version that
 * QEMU currently supports. The minimum API will be incremented if an
 * API needs to be deprecated.
 *
 * The plugins export the API they were built against by exposing the
 * symbol qemu_plugin_version which can be checked.
 */

extern QEMU_PLUGIN_EXPORT int qemu_plugin_version;

#define QEMU_PLUGIN_VERSION 0

typedef struct {
    /* string describing architecture */
    const char *target_name;
    struct {
        int min;
        int cur;
    } version;
    /* is this a full system emulation? */
    bool system_emulation;
    union {
        /*
         * smp_vcpus may change if vCPUs can be hot-plugged, max_vcpus
         * is the system-wide limit.
         */
        struct {
            int smp_vcpus;
            int max_vcpus;
        } system;
    };
} qemu_info_t;

/**
 * qemu_plugin_install() - Install a plugin
 * @id: this plugin's opaque ID
 * @info: a block describing some details about the guest
 * @argc: number of arguments
 * @argv: array of arguments (@argc elements)
 *
 * All plugins must export this symbol.
 *
 * Note: Calling qemu_plugin_uninstall() from this function is a bug. To raise
 * an error during install, return !0.
 *
 * Note: @info is only live during the call. Copy any information we
 * want to keep.
 *
 * Note: @argv remains valid throughout the lifetime of the loaded plugin.
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv);

/*
 * Prototypes for the various callback styles we will be registering
 * in the following functions.
 */
typedef void (*qemu_plugin_simple_cb_t)(qemu_plugin_id_t id);

typedef void (*qemu_plugin_udata_cb_t)(qemu_plugin_id_t id, void *userdata);

typedef void (*qemu_plugin_vcpu_simple_cb_t)(qemu_plugin_id_t id,
                                             unsigned int vcpu_index);

typedef void (*qemu_plugin_vcpu_udata_cb_t)(unsigned int vcpu_index,
                                            void *userdata);

/**
 * qemu_plugin_uninstall() - Uninstall a plugin
 * @id: this plugin's opaque ID
 * @cb: callback to be called once the plugin has been removed
 *
 * Do NOT assume that the plugin has been uninstalled once this function
 * returns. Plugins are uninstalled asynchronously, and therefore the given
 * plugin receives callbacks until @cb is called.
 *
 * Note: Calling this function from qemu_plugin_install() is a bug.
 */
void qemu_plugin_uninstall(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb);

/**
 * qemu_plugin_reset() - Reset a plugin
 * @id: this plugin's opaque ID
 * @cb: callback to be called once the plugin has been reset
 *
 * Unregisters all callbacks for the plugin given by @id.
 *
 * Do NOT assume that the plugin has been reset once this function returns.
 * Plugins are reset asynchronously, and therefore the given plugin receives
 * callbacks until @cb is called.
 */
void qemu_plugin_reset(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_init_cb() - register a vCPU initialization callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU is initialized.
 *
 * See also: qemu_plugin_register_vcpu_exit_cb()
 */
void qemu_plugin_register_vcpu_init_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_exit_cb() - register a vCPU exit callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU exits.
 *
 * See also: qemu_plugin_register_vcpu_init_cb()
 */
void qemu_plugin_register_vcpu_exit_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_idle_cb() - register a vCPU idle callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU idles.
 */
void qemu_plugin_register_vcpu_idle_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_resume_cb() - register a vCPU resume callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU resumes execution.
 */
void qemu_plugin_register_vcpu_resume_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_simple_cb_t cb);

/*
 * Opaque types that the plugin is given during the translation and
 * instrumentation phase.
 */
struct qemu_plugin_tb;
struct qemu_plugin_insn;

enum qemu_plugin_cb_flags {
    QEMU_PLUGIN_CB_NO_REGS, /* callback does not access the CPU's regs */
    QEMU_PLUGIN_CB_R_REGS,  /* callback reads the CPU's regs */
    QEMU_PLUGIN_CB_RW_REGS, /* callback reads and writes the CPU's regs */
};

enum qemu_plugin_mem_rw {
    QEMU_PLUGIN_MEM_R = 1,
    QEMU_PLUGIN_MEM_W,
    QEMU_PLUGIN_MEM_RW,
};

/**
 * qemu_plugin_register_vcpu_tb_trans_cb() - register a translate cb
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a translation occurs. The @cb
 * function is passed an opaque qemu_plugin_type which it can query
 * for additional information including the list of translated
 * instructions. At this point the plugin can register further
 * callbacks to be triggered when the block or individual instruction
 * executes.
 */
typedef void (*qemu_plugin_vcpu_tb_trans_cb_t)(qemu_plugin_id_t id,
                                               struct qemu_plugin_tb *tb);

void qemu_plugin_register_vcpu_tb_trans_cb(qemu_plugin_id_t id,
                                           qemu_plugin_vcpu_tb_trans_cb_t cb);

/**
 * qemu_plugin_register_vcpu_tb_trans_exec_cb() - register execution callback
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called every time a translated unit executes.
 */
void qemu_plugin_register_vcpu_tb_exec_cb(struct qemu_plugin_tb *tb,
                                          qemu_plugin_vcpu_udata_cb_t cb,
                                          enum qemu_plugin_cb_flags flags,
                                          void *userdata);

enum qemu_plugin_op {
    QEMU_PLUGIN_INLINE_ADD_U64,
};

/**
 * qemu_plugin_register_vcpu_tb_trans_exec_inline() - execution inline op
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @op: the type of qemu_plugin_op (e.g. ADD_U64)
 * @ptr: the target memory location for the op
 * @imm: the op data (e.g. 1)
 *
 * Insert an inline op to every time a translated unit executes.
 * Useful if you just want to increment a single counter somewhere in
 * memory.
 */
void qemu_plugin_register_vcpu_tb_exec_inline(struct qemu_plugin_tb *tb,
                                              enum qemu_plugin_op op,
                                              void *ptr, uint64_t imm);

/**
 * qemu_plugin_register_vcpu_insn_exec_cb() - register insn execution cb
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called every time an instruction is executed
 */
void qemu_plugin_register_vcpu_insn_exec_cb(struct qemu_plugin_insn *insn,
                                            qemu_plugin_vcpu_udata_cb_t cb,
                                            enum qemu_plugin_cb_flags flags,
                                            void *userdata);

/**
 * qemu_plugin_register_vcpu_insn_exec_inline() - insn execution inline op
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @cb: callback function
 * @op: the type of qemu_plugin_op (e.g. ADD_U64)
 * @ptr: the target memory location for the op
 * @imm: the op data (e.g. 1)
 *
 * Insert an inline op to every time an instruction executes. Useful
 * if you just want to increment a single counter somewhere in memory.
 */
void qemu_plugin_register_vcpu_insn_exec_inline(struct qemu_plugin_insn *insn,
                                                enum qemu_plugin_op op,
                                                void *ptr, uint64_t imm);

/*
 * Helpers to query information about the instructions in a block
 */
size_t qemu_plugin_tb_n_insns(const struct qemu_plugin_tb *tb);

uint64_t qemu_plugin_tb_vaddr(const struct qemu_plugin_tb *tb);

struct qemu_plugin_insn *
qemu_plugin_tb_get_insn(const struct qemu_plugin_tb *tb, size_t idx);

const void *qemu_plugin_insn_data(const struct qemu_plugin_insn *insn);

size_t qemu_plugin_insn_size(const struct qemu_plugin_insn *insn);

uint64_t qemu_plugin_insn_vaddr(const struct qemu_plugin_insn *insn);
void *qemu_plugin_insn_haddr(const struct qemu_plugin_insn *insn);

/*
 * Memory Instrumentation
 *
 * The anonymous qemu_plugin_meminfo_t and qemu_plugin_hwaddr types
 * can be used in queries to QEMU to get more information about a
 * given memory access.
 */
typedef uint32_t qemu_plugin_meminfo_t;
struct qemu_plugin_hwaddr;

/* meminfo queries */
unsigned int qemu_plugin_mem_size_shift(qemu_plugin_meminfo_t info);
bool qemu_plugin_mem_is_sign_extended(qemu_plugin_meminfo_t info);
bool qemu_plugin_mem_is_big_endian(qemu_plugin_meminfo_t info);
bool qemu_plugin_mem_is_store(qemu_plugin_meminfo_t info);

/*
 * qemu_plugin_get_hwaddr():
 * @vaddr: the virtual address of the memory operation
 *
 * For system emulation returns a qemu_plugin_hwaddr handle to query
 * details about the actual physical address backing the virtual
 * address. For linux-user guests it just returns NULL.
 *
 * This handle is *only* valid for the duration of the callback. Any
 * information about the handle should be recovered before the
 * callback returns.
 */
struct qemu_plugin_hwaddr *qemu_plugin_get_hwaddr(qemu_plugin_meminfo_t info,
                                                  uint64_t vaddr);

/*
 * The following additional queries can be run on the hwaddr structure
 * to return information about it. For non-IO accesses the device
 * offset will be into the appropriate block of RAM.
 */
bool qemu_plugin_hwaddr_is_io(const struct qemu_plugin_hwaddr *haddr);
uint64_t qemu_plugin_hwaddr_device_offset(const struct qemu_plugin_hwaddr *haddr);

typedef void
(*qemu_plugin_vcpu_mem_cb_t)(unsigned int vcpu_index,
                             qemu_plugin_meminfo_t info, uint64_t vaddr,
                             void *userdata);

void qemu_plugin_register_vcpu_mem_cb(struct qemu_plugin_insn *insn,
                                      qemu_plugin_vcpu_mem_cb_t cb,
                                      enum qemu_plugin_cb_flags flags,
                                      enum qemu_plugin_mem_rw rw,
                                      void *userdata);

void qemu_plugin_register_vcpu_mem_inline(struct qemu_plugin_insn *insn,
                                          enum qemu_plugin_mem_rw rw,
                                          enum qemu_plugin_op op, void *ptr,
                                          uint64_t imm);



typedef void
(*qemu_plugin_vcpu_syscall_cb_t)(qemu_plugin_id_t id, unsigned int vcpu_index,
                                 int64_t num, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4, uint64_t a5,
                                 uint64_t a6, uint64_t a7, uint64_t a8);

void qemu_plugin_register_vcpu_syscall_cb(qemu_plugin_id_t id,
                                          qemu_plugin_vcpu_syscall_cb_t cb);

typedef void
(*qemu_plugin_vcpu_syscall_ret_cb_t)(qemu_plugin_id_t id, unsigned int vcpu_idx,
                                     int64_t num, int64_t ret);

void
qemu_plugin_register_vcpu_syscall_ret_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_syscall_ret_cb_t cb);


/**
 * qemu_plugin_insn_disas() - return disassembly string for instruction
 * @insn: instruction reference
 *
 * Returns an allocated string containing the disassembly
 */

char *qemu_plugin_insn_disas(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_vcpu_for_each() - iterate over the existing vCPU
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called once for each existing vCPU.
 *
 * See also: qemu_plugin_register_vcpu_init_cb()
 */
void qemu_plugin_vcpu_for_each(qemu_plugin_id_t id,
                               qemu_plugin_vcpu_simple_cb_t cb);

void qemu_plugin_register_flush_cb(qemu_plugin_id_t id,
                                   qemu_plugin_simple_cb_t cb);

void qemu_plugin_register_atexit_cb(qemu_plugin_id_t id,
                                    qemu_plugin_udata_cb_t cb, void *userdata);

/* returns -1 in user-mode */
int qemu_plugin_n_vcpus(void);

/* returns -1 in user-mode */
int qemu_plugin_n_max_vcpus(void);

/**
 * qemu_plugin_outs() - output string via QEMU's logging system
 * @string: a string
 */
void qemu_plugin_outs(const char *string);

#endif /* QEMU_PLUGIN_API_H */
