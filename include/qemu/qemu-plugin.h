/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_QEMU_PLUGIN_H
#define QEMU_QEMU_PLUGIN_H

#include <glib.h>
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
  #ifdef CONFIG_PLUGIN
    #define QEMU_PLUGIN_EXPORT __declspec(dllimport)
    #define QEMU_PLUGIN_API __declspec(dllexport)
  #else
    #define QEMU_PLUGIN_EXPORT __declspec(dllexport)
    #define QEMU_PLUGIN_API __declspec(dllimport)
  #endif
  #define QEMU_PLUGIN_LOCAL
#else
  #define QEMU_PLUGIN_EXPORT __attribute__((visibility("default")))
  #define QEMU_PLUGIN_LOCAL  __attribute__((visibility("hidden")))
  #define QEMU_PLUGIN_API
#endif

/**
 * typedef qemu_plugin_id_t - Unique plugin ID
 */
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
 *
 * version 2:
 * - removed qemu_plugin_n_vcpus and qemu_plugin_n_max_vcpus
 * - Remove qemu_plugin_register_vcpu_{tb, insn, mem}_exec_inline.
 *   Those functions are replaced by *_per_vcpu variants, which guarantee
 *   thread-safety for operations.
 *
 * version 3:
 * - modified arguments and return value of qemu_plugin_insn_data to copy
 *   the data into a user-provided buffer instead of returning a pointer
 *   to the data.
 *
 * version 4:
 * - added qemu_plugin_read_memory_vaddr
 */

extern QEMU_PLUGIN_EXPORT int qemu_plugin_version;

#define QEMU_PLUGIN_VERSION 4

/**
 * struct qemu_info_t - system information for plugins
 *
 * This structure provides for some limited information about the
 * system to allow the plugin to make decisions on how to proceed. For
 * example it might only be suitable for running on some guest
 * architectures or when under full system emulation.
 */
typedef struct qemu_info_t {
    /** @target_name: string describing architecture */
    const char *target_name;
    /** @version: minimum and current plugin API level */
    struct {
        int min;
        int cur;
    } version;
    /** @system_emulation: is this a full system emulation? */
    bool system_emulation;
    union {
        /** @system: information relevant to system emulation */
        struct {
            /** @system.smp_vcpus: initial number of vCPUs */
            int smp_vcpus;
            /** @system.max_vcpus: maximum possible number of vCPUs */
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
 * All plugins must export this symbol which is called when the plugin
 * is first loaded. Calling qemu_plugin_uninstall() from this function
 * is a bug.
 *
 * Note: @info is only live during the call. Copy any information we
 * want to keep. @argv remains valid throughout the lifetime of the
 * loaded plugin.
 *
 * Return: 0 on successful loading, !0 for an error.
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv);

/**
 * typedef qemu_plugin_simple_cb_t - simple callback
 * @id: the unique qemu_plugin_id_t
 *
 * This callback passes no information aside from the unique @id.
 */
typedef void (*qemu_plugin_simple_cb_t)(qemu_plugin_id_t id);

/**
 * typedef qemu_plugin_udata_cb_t - callback with user data
 * @id: the unique qemu_plugin_id_t
 * @userdata: a pointer to some user data supplied when the callback
 * was registered.
 */
typedef void (*qemu_plugin_udata_cb_t)(qemu_plugin_id_t id, void *userdata);

/**
 * typedef qemu_plugin_vcpu_simple_cb_t - vcpu callback
 * @id: the unique qemu_plugin_id_t
 * @vcpu_index: the current vcpu context
 */
typedef void (*qemu_plugin_vcpu_simple_cb_t)(qemu_plugin_id_t id,
                                             unsigned int vcpu_index);

/**
 * typedef qemu_plugin_vcpu_udata_cb_t - vcpu callback
 * @vcpu_index: the current vcpu context
 * @userdata: a pointer to some user data supplied when the callback
 * was registered.
 */
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
QEMU_PLUGIN_API
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
QEMU_PLUGIN_API
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
QEMU_PLUGIN_API
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
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_exit_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_idle_cb() - register a vCPU idle callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU idles.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_idle_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_resume_cb() - register a vCPU resume callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU resumes execution.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_resume_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_simple_cb_t cb);

/** struct qemu_plugin_tb - Opaque handle for a translation block */
struct qemu_plugin_tb;
/** struct qemu_plugin_insn - Opaque handle for a translated instruction */
struct qemu_plugin_insn;
/** struct qemu_plugin_scoreboard - Opaque handle for a scoreboard */
struct qemu_plugin_scoreboard;

/**
 * typedef qemu_plugin_u64 - uint64_t member of an entry in a scoreboard
 *
 * This field allows to access a specific uint64_t member in one given entry,
 * located at a specified offset. Inline operations expect this as entry.
 */
typedef struct {
    struct qemu_plugin_scoreboard *score;
    size_t offset;
} qemu_plugin_u64;

/**
 * enum qemu_plugin_cb_flags - type of callback
 *
 * @QEMU_PLUGIN_CB_NO_REGS: callback does not access the CPU's regs
 * @QEMU_PLUGIN_CB_R_REGS: callback reads the CPU's regs
 * @QEMU_PLUGIN_CB_RW_REGS: callback reads and writes the CPU's regs
 *
 * Note: currently QEMU_PLUGIN_CB_RW_REGS is unused, plugins cannot change
 * system register state.
 */
enum qemu_plugin_cb_flags {
    QEMU_PLUGIN_CB_NO_REGS,
    QEMU_PLUGIN_CB_R_REGS,
    QEMU_PLUGIN_CB_RW_REGS,
};

enum qemu_plugin_mem_rw {
    QEMU_PLUGIN_MEM_R = 1,
    QEMU_PLUGIN_MEM_W,
    QEMU_PLUGIN_MEM_RW,
};

enum qemu_plugin_mem_value_type {
    QEMU_PLUGIN_MEM_VALUE_U8,
    QEMU_PLUGIN_MEM_VALUE_U16,
    QEMU_PLUGIN_MEM_VALUE_U32,
    QEMU_PLUGIN_MEM_VALUE_U64,
    QEMU_PLUGIN_MEM_VALUE_U128,
};

/* typedef qemu_plugin_mem_value - value accessed during a load/store */
typedef struct {
    enum qemu_plugin_mem_value_type type;
    union {
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        struct {
            uint64_t low;
            uint64_t high;
        } u128;
    } data;
} qemu_plugin_mem_value;

/**
 * enum qemu_plugin_cond - condition to enable callback
 *
 * @QEMU_PLUGIN_COND_NEVER: false
 * @QEMU_PLUGIN_COND_ALWAYS: true
 * @QEMU_PLUGIN_COND_EQ: is equal?
 * @QEMU_PLUGIN_COND_NE: is not equal?
 * @QEMU_PLUGIN_COND_LT: is less than?
 * @QEMU_PLUGIN_COND_LE: is less than or equal?
 * @QEMU_PLUGIN_COND_GT: is greater than?
 * @QEMU_PLUGIN_COND_GE: is greater than or equal?
 */
enum qemu_plugin_cond {
    QEMU_PLUGIN_COND_NEVER,
    QEMU_PLUGIN_COND_ALWAYS,
    QEMU_PLUGIN_COND_EQ,
    QEMU_PLUGIN_COND_NE,
    QEMU_PLUGIN_COND_LT,
    QEMU_PLUGIN_COND_LE,
    QEMU_PLUGIN_COND_GT,
    QEMU_PLUGIN_COND_GE,
};

/**
 * typedef qemu_plugin_vcpu_tb_trans_cb_t - translation callback
 * @id: unique plugin id
 * @tb: opaque handle used for querying and instrumenting a block.
 */
typedef void (*qemu_plugin_vcpu_tb_trans_cb_t)(qemu_plugin_id_t id,
                                               struct qemu_plugin_tb *tb);

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
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_tb_trans_cb(qemu_plugin_id_t id,
                                           qemu_plugin_vcpu_tb_trans_cb_t cb);

/**
 * qemu_plugin_register_vcpu_tb_exec_cb() - register execution callback
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called every time a translated unit executes.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_tb_exec_cb(struct qemu_plugin_tb *tb,
                                          qemu_plugin_vcpu_udata_cb_t cb,
                                          enum qemu_plugin_cb_flags flags,
                                          void *userdata);

/**
 * qemu_plugin_register_vcpu_tb_exec_cond_cb() - register conditional callback
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @cb: callback function
 * @cond: condition to enable callback
 * @entry: first operand for condition
 * @imm: second operand for condition
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called when a translated unit executes if
 * entry @cond imm is true.
 * If condition is QEMU_PLUGIN_COND_ALWAYS, condition is never interpreted and
 * this function is equivalent to qemu_plugin_register_vcpu_tb_exec_cb.
 * If condition QEMU_PLUGIN_COND_NEVER, condition is never interpreted and
 * callback is never installed.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_tb_exec_cond_cb(struct qemu_plugin_tb *tb,
                                               qemu_plugin_vcpu_udata_cb_t cb,
                                               enum qemu_plugin_cb_flags flags,
                                               enum qemu_plugin_cond cond,
                                               qemu_plugin_u64 entry,
                                               uint64_t imm,
                                               void *userdata);

/**
 * enum qemu_plugin_op - describes an inline op
 *
 * @QEMU_PLUGIN_INLINE_ADD_U64: add an immediate value uint64_t
 * @QEMU_PLUGIN_INLINE_STORE_U64: store an immediate value uint64_t
 */

enum qemu_plugin_op {
    QEMU_PLUGIN_INLINE_ADD_U64,
    QEMU_PLUGIN_INLINE_STORE_U64,
};

/**
 * qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu() - execution inline op
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @op: the type of qemu_plugin_op (e.g. ADD_U64)
 * @entry: entry to run op
 * @imm: the op data (e.g. 1)
 *
 * Insert an inline op on a given scoreboard entry.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
    struct qemu_plugin_tb *tb,
    enum qemu_plugin_op op,
    qemu_plugin_u64 entry,
    uint64_t imm);

/**
 * qemu_plugin_register_vcpu_insn_exec_cb() - register insn execution cb
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called every time an instruction is executed
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_insn_exec_cb(struct qemu_plugin_insn *insn,
                                            qemu_plugin_vcpu_udata_cb_t cb,
                                            enum qemu_plugin_cb_flags flags,
                                            void *userdata);

/**
 * qemu_plugin_register_vcpu_insn_exec_cond_cb() - conditional insn execution cb
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @cond: condition to enable callback
 * @entry: first operand for condition
 * @imm: second operand for condition
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called when an instruction executes if
 * entry @cond imm is true.
 * If condition is QEMU_PLUGIN_COND_ALWAYS, condition is never interpreted and
 * this function is equivalent to qemu_plugin_register_vcpu_insn_exec_cb.
 * If condition QEMU_PLUGIN_COND_NEVER, condition is never interpreted and
 * callback is never installed.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_insn_exec_cond_cb(
    struct qemu_plugin_insn *insn,
    qemu_plugin_vcpu_udata_cb_t cb,
    enum qemu_plugin_cb_flags flags,
    enum qemu_plugin_cond cond,
    qemu_plugin_u64 entry,
    uint64_t imm,
    void *userdata);

/**
 * qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu() - insn exec inline op
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @op: the type of qemu_plugin_op (e.g. ADD_U64)
 * @entry: entry to run op
 * @imm: the op data (e.g. 1)
 *
 * Insert an inline op to every time an instruction executes.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
    struct qemu_plugin_insn *insn,
    enum qemu_plugin_op op,
    qemu_plugin_u64 entry,
    uint64_t imm);

/**
 * qemu_plugin_tb_n_insns() - query helper for number of insns in TB
 * @tb: opaque handle to TB passed to callback
 *
 * Returns: number of instructions in this block
 */
QEMU_PLUGIN_API
size_t qemu_plugin_tb_n_insns(const struct qemu_plugin_tb *tb);

/**
 * qemu_plugin_tb_vaddr() - query helper for vaddr of TB start
 * @tb: opaque handle to TB passed to callback
 *
 * Returns: virtual address of block start
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_tb_vaddr(const struct qemu_plugin_tb *tb);

/**
 * qemu_plugin_tb_get_insn() - retrieve handle for instruction
 * @tb: opaque handle to TB passed to callback
 * @idx: instruction number, 0 indexed
 *
 * The returned handle can be used in follow up helper queries as well
 * as when instrumenting an instruction. It is only valid for the
 * lifetime of the callback.
 *
 * Returns: opaque handle to instruction
 */
QEMU_PLUGIN_API
struct qemu_plugin_insn *
qemu_plugin_tb_get_insn(const struct qemu_plugin_tb *tb, size_t idx);

/**
 * qemu_plugin_insn_data() - copy instruction data
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 * @dest: destination into which data is copied
 * @len: length of dest
 *
 * Returns the number of bytes copied, minimum of @len and insn size.
 */
QEMU_PLUGIN_API
size_t qemu_plugin_insn_data(const struct qemu_plugin_insn *insn,
                             void *dest, size_t len);

/**
 * qemu_plugin_insn_size() - return size of instruction
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Returns: size of instruction in bytes
 */
QEMU_PLUGIN_API
size_t qemu_plugin_insn_size(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_vaddr() - return vaddr of instruction
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Returns: virtual address of instruction
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_insn_vaddr(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_haddr() - return hardware addr of instruction
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Returns: hardware (physical) target address of instruction
 */
QEMU_PLUGIN_API
void *qemu_plugin_insn_haddr(const struct qemu_plugin_insn *insn);

/**
 * typedef qemu_plugin_meminfo_t - opaque memory transaction handle
 *
 * This can be further queried using the qemu_plugin_mem_* query
 * functions.
 */
typedef uint32_t qemu_plugin_meminfo_t;
/** struct qemu_plugin_hwaddr - opaque hw address handle */
struct qemu_plugin_hwaddr;

/**
 * qemu_plugin_mem_size_shift() - get size of access
 * @info: opaque memory transaction handle
 *
 * Returns: size of access in ^2 (0=byte, 1=16bit, 2=32bit etc...)
 */
QEMU_PLUGIN_API
unsigned int qemu_plugin_mem_size_shift(qemu_plugin_meminfo_t info);
/**
 * qemu_plugin_mem_is_sign_extended() - was the access sign extended
 * @info: opaque memory transaction handle
 *
 * Returns: true if it was, otherwise false
 */
QEMU_PLUGIN_API
bool qemu_plugin_mem_is_sign_extended(qemu_plugin_meminfo_t info);
/**
 * qemu_plugin_mem_is_big_endian() - was the access big endian
 * @info: opaque memory transaction handle
 *
 * Returns: true if it was, otherwise false
 */
QEMU_PLUGIN_API
bool qemu_plugin_mem_is_big_endian(qemu_plugin_meminfo_t info);
/**
 * qemu_plugin_mem_is_store() - was the access a store
 * @info: opaque memory transaction handle
 *
 * Returns: true if it was, otherwise false
 */
QEMU_PLUGIN_API
bool qemu_plugin_mem_is_store(qemu_plugin_meminfo_t info);

/**
 * qemu_plugin_mem_get_value() - return last value loaded/stored
 * @info: opaque memory transaction handle
 *
 * Returns: memory value
 */
QEMU_PLUGIN_API
qemu_plugin_mem_value qemu_plugin_mem_get_value(qemu_plugin_meminfo_t info);

/**
 * qemu_plugin_get_hwaddr() - return handle for memory operation
 * @info: opaque memory info structure
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
QEMU_PLUGIN_API
struct qemu_plugin_hwaddr *qemu_plugin_get_hwaddr(qemu_plugin_meminfo_t info,
                                                  uint64_t vaddr);

/*
 * The following additional queries can be run on the hwaddr structure to
 * return information about it - namely whether it is for an IO access and the
 * physical address associated with the access.
 */

/**
 * qemu_plugin_hwaddr_is_io() - query whether memory operation is IO
 * @haddr: address handle from qemu_plugin_get_hwaddr()
 *
 * Returns true if the handle's memory operation is to memory-mapped IO, or
 * false if it is to RAM
 */
QEMU_PLUGIN_API
bool qemu_plugin_hwaddr_is_io(const struct qemu_plugin_hwaddr *haddr);

/**
 * qemu_plugin_hwaddr_phys_addr() - query physical address for memory operation
 * @haddr: address handle from qemu_plugin_get_hwaddr()
 *
 * Returns the physical address associated with the memory operation
 *
 * Note that the returned physical address may not be unique if you are dealing
 * with multiple address spaces.
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_hwaddr_phys_addr(const struct qemu_plugin_hwaddr *haddr);

/*
 * Returns a string representing the device. The string is valid for
 * the lifetime of the plugin.
 */
QEMU_PLUGIN_API
const char *qemu_plugin_hwaddr_device_name(const struct qemu_plugin_hwaddr *h);

/**
 * typedef qemu_plugin_vcpu_mem_cb_t - memory callback function type
 * @vcpu_index: the executing vCPU
 * @info: an opaque handle for further queries about the memory
 * @vaddr: the virtual address of the transaction
 * @userdata: any user data attached to the callback
 */
typedef void (*qemu_plugin_vcpu_mem_cb_t) (unsigned int vcpu_index,
                                           qemu_plugin_meminfo_t info,
                                           uint64_t vaddr,
                                           void *userdata);

/**
 * qemu_plugin_register_vcpu_mem_cb() - register memory access callback
 * @insn: handle for instruction to instrument
 * @cb: callback of type qemu_plugin_vcpu_mem_cb_t
 * @flags: (currently unused) callback flags
 * @rw: monitor reads, writes or both
 * @userdata: opaque pointer for userdata
 *
 * This registers a full callback for every memory access generated by
 * an instruction. If the instruction doesn't access memory no
 * callback will be made.
 *
 * The callback reports the vCPU the access took place on, the virtual
 * address of the access and a handle for further queries. The user
 * can attach some userdata to the callback for additional purposes.
 *
 * Other execution threads will continue to execute during the
 * callback so the plugin is responsible for ensuring it doesn't get
 * confused by making appropriate use of locking if required.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_mem_cb(struct qemu_plugin_insn *insn,
                                      qemu_plugin_vcpu_mem_cb_t cb,
                                      enum qemu_plugin_cb_flags flags,
                                      enum qemu_plugin_mem_rw rw,
                                      void *userdata);

/**
 * qemu_plugin_register_vcpu_mem_inline_per_vcpu() - inline op for mem access
 * @insn: handle for instruction to instrument
 * @rw: apply to reads, writes or both
 * @op: the op, of type qemu_plugin_op
 * @entry: entry to run op
 * @imm: immediate data for @op
 *
 * This registers a inline op every memory access generated by the
 * instruction.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_mem_inline_per_vcpu(
    struct qemu_plugin_insn *insn,
    enum qemu_plugin_mem_rw rw,
    enum qemu_plugin_op op,
    qemu_plugin_u64 entry,
    uint64_t imm);

/**
 * qemu_plugin_request_time_control() - request the ability to control time
 *
 * This grants the plugin the ability to control system time. Only one
 * plugin can control time so if multiple plugins request the ability
 * all but the first will fail.
 *
 * Returns an opaque handle or NULL if fails
 */
QEMU_PLUGIN_API
const void *qemu_plugin_request_time_control(void);

/**
 * qemu_plugin_update_ns() - update system emulation time
 * @handle: opaque handle returned by qemu_plugin_request_time_control()
 * @time: time in nanoseconds
 *
 * This allows an appropriately authorised plugin (i.e. holding the
 * time control handle) to move system time forward to @time. For
 * user-mode emulation the time is not changed by this as all reported
 * time comes from the host kernel.
 *
 * Start time is 0.
 */
QEMU_PLUGIN_API
void qemu_plugin_update_ns(const void *handle, int64_t time);

typedef void
(*qemu_plugin_vcpu_syscall_cb_t)(qemu_plugin_id_t id, unsigned int vcpu_index,
                                 int64_t num, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4, uint64_t a5,
                                 uint64_t a6, uint64_t a7, uint64_t a8);

QEMU_PLUGIN_API
void qemu_plugin_register_vcpu_syscall_cb(qemu_plugin_id_t id,
                                          qemu_plugin_vcpu_syscall_cb_t cb);

typedef void
(*qemu_plugin_vcpu_syscall_ret_cb_t)(qemu_plugin_id_t id, unsigned int vcpu_idx,
                                     int64_t num, int64_t ret);

QEMU_PLUGIN_API
void
qemu_plugin_register_vcpu_syscall_ret_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_syscall_ret_cb_t cb);


/**
 * qemu_plugin_insn_disas() - return disassembly string for instruction
 * @insn: instruction reference
 *
 * Returns an allocated string containing the disassembly
 */

QEMU_PLUGIN_API
char *qemu_plugin_insn_disas(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_symbol() - best effort symbol lookup
 * @insn: instruction reference
 *
 * Return a static string referring to the symbol. This is dependent
 * on the binary QEMU is running having provided a symbol table.
 */
QEMU_PLUGIN_API
const char *qemu_plugin_insn_symbol(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_vcpu_for_each() - iterate over the existing vCPU
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called once for each existing vCPU.
 *
 * See also: qemu_plugin_register_vcpu_init_cb()
 */
QEMU_PLUGIN_API
void qemu_plugin_vcpu_for_each(qemu_plugin_id_t id,
                               qemu_plugin_vcpu_simple_cb_t cb);

QEMU_PLUGIN_API
void qemu_plugin_register_flush_cb(qemu_plugin_id_t id,
                                   qemu_plugin_simple_cb_t cb);

/**
 * qemu_plugin_register_atexit_cb() - register exit callback
 * @id: plugin ID
 * @cb: callback
 * @userdata: user data for callback
 *
 * The @cb function is called once execution has finished. Plugins
 * should be able to free all their resources at this point much like
 * after a reset/uninstall callback is called.
 *
 * In user-mode it is possible a few un-instrumented instructions from
 * child threads may run before the host kernel reaps the threads.
 */
QEMU_PLUGIN_API
void qemu_plugin_register_atexit_cb(qemu_plugin_id_t id,
                                    qemu_plugin_udata_cb_t cb, void *userdata);

/* returns how many vcpus were started at this point */
QEMU_PLUGIN_API
int qemu_plugin_num_vcpus(void);

/**
 * qemu_plugin_outs() - output string via QEMU's logging system
 * @string: a string
 */
QEMU_PLUGIN_API
void qemu_plugin_outs(const char *string);

/**
 * qemu_plugin_bool_parse() - parses a boolean argument in the form of
 * "<argname>=[on|yes|true|off|no|false]"
 *
 * @name: argument name, the part before the equals sign
 * @val: argument value, what's after the equals sign
 * @ret: output return value
 *
 * returns true if the combination @name=@val parses correctly to a boolean
 * argument, and false otherwise
 */
QEMU_PLUGIN_API
bool qemu_plugin_bool_parse(const char *name, const char *val, bool *ret);

/**
 * qemu_plugin_path_to_binary() - path to binary file being executed
 *
 * Return a string representing the path to the binary. For user-mode
 * this is the main executable. For system emulation we currently
 * return NULL. The user should g_free() the string once no longer
 * needed.
 */
QEMU_PLUGIN_API
const char *qemu_plugin_path_to_binary(void);

/**
 * qemu_plugin_start_code() - returns start of text segment
 *
 * Returns the nominal start address of the main text segment in
 * user-mode. Currently returns 0 for system emulation.
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_start_code(void);

/**
 * qemu_plugin_end_code() - returns end of text segment
 *
 * Returns the nominal end address of the main text segment in
 * user-mode. Currently returns 0 for system emulation.
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_end_code(void);

/**
 * qemu_plugin_entry_code() - returns start address for module
 *
 * Returns the nominal entry address of the main text segment in
 * user-mode. Currently returns 0 for system emulation.
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_entry_code(void);

/** struct qemu_plugin_register - Opaque handle for register access */
struct qemu_plugin_register;

/**
 * typedef qemu_plugin_reg_descriptor - register descriptions
 *
 * @handle: opaque handle for retrieving value with qemu_plugin_read_register
 * @name: register name
 * @feature: optional feature descriptor, can be NULL
 */
typedef struct {
    struct qemu_plugin_register *handle;
    const char *name;
    const char *feature;
} qemu_plugin_reg_descriptor;

/**
 * qemu_plugin_get_registers() - return register list for current vCPU
 *
 * Returns a potentially empty GArray of qemu_plugin_reg_descriptor.
 * Caller frees the array (but not the const strings).
 *
 * Should be used from a qemu_plugin_register_vcpu_init_cb() callback
 * after the vCPU is initialised, i.e. in the vCPU context.
 */
QEMU_PLUGIN_API
GArray *qemu_plugin_get_registers(void);

/**
 * qemu_plugin_read_memory_vaddr() - read from memory using a virtual address
 *
 * @addr: A virtual address to read from
 * @data: A byte array to store data into
 * @len: The number of bytes to read, starting from @addr
 *
 * @len bytes of data is read starting at @addr and stored into @data. If @data
 * is not large enough to hold @len bytes, it will be expanded to the necessary
 * size, reallocating if necessary. @len must be greater than 0.
 *
 * This function does not ensure writes are flushed prior to reading, so
 * callers should take care when calling this function in plugin callbacks to
 * avoid attempting to read data which may not yet be written and should use
 * the memory callback API instead.
 *
 * Returns true on success and false on failure.
 */
QEMU_PLUGIN_API
bool qemu_plugin_read_memory_vaddr(uint64_t addr,
                                   GByteArray *data, size_t len);

/**
 * qemu_plugin_read_register() - read register for current vCPU
 *
 * @handle: a @qemu_plugin_reg_handle handle
 * @buf: A GByteArray for the data owned by the plugin
 *
 * This function is only available in a context that register read access is
 * explicitly requested via the QEMU_PLUGIN_CB_R_REGS flag.
 *
 * Returns the size of the read register. The content of @buf is in target byte
 * order. On failure returns -1.
 */
QEMU_PLUGIN_API
int qemu_plugin_read_register(struct qemu_plugin_register *handle,
                              GByteArray *buf);

/**
 * qemu_plugin_scoreboard_new() - alloc a new scoreboard
 *
 * @element_size: size (in bytes) for one entry
 *
 * Returns a pointer to a new scoreboard. It must be freed using
 * qemu_plugin_scoreboard_free.
 */
QEMU_PLUGIN_API
struct qemu_plugin_scoreboard *qemu_plugin_scoreboard_new(size_t element_size);

/**
 * qemu_plugin_scoreboard_free() - free a scoreboard
 * @score: scoreboard to free
 */
QEMU_PLUGIN_API
void qemu_plugin_scoreboard_free(struct qemu_plugin_scoreboard *score);

/**
 * qemu_plugin_scoreboard_find() - get pointer to an entry of a scoreboard
 * @score: scoreboard to query
 * @vcpu_index: entry index
 *
 * Returns address of entry of a scoreboard matching a given vcpu_index. This
 * address can be modified later if scoreboard is resized.
 */
QEMU_PLUGIN_API
void *qemu_plugin_scoreboard_find(struct qemu_plugin_scoreboard *score,
                                  unsigned int vcpu_index);

/* Macros to define a qemu_plugin_u64 */
#define qemu_plugin_scoreboard_u64(score) \
    (qemu_plugin_u64) {score, 0}
#define qemu_plugin_scoreboard_u64_in_struct(score, type, member) \
    (qemu_plugin_u64) {score, offsetof(type, member)}

/**
 * qemu_plugin_u64_add() - add a value to a qemu_plugin_u64 for a given vcpu
 * @entry: entry to query
 * @vcpu_index: entry index
 * @added: value to add
 */
QEMU_PLUGIN_API
void qemu_plugin_u64_add(qemu_plugin_u64 entry, unsigned int vcpu_index,
                         uint64_t added);

/**
 * qemu_plugin_u64_get() - get value of a qemu_plugin_u64 for a given vcpu
 * @entry: entry to query
 * @vcpu_index: entry index
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_u64_get(qemu_plugin_u64 entry, unsigned int vcpu_index);

/**
 * qemu_plugin_u64_set() - set value of a qemu_plugin_u64 for a given vcpu
 * @entry: entry to query
 * @vcpu_index: entry index
 * @val: new value
 */
QEMU_PLUGIN_API
void qemu_plugin_u64_set(qemu_plugin_u64 entry, unsigned int vcpu_index,
                         uint64_t val);

/**
 * qemu_plugin_u64_sum() - return sum of all vcpu entries in a scoreboard
 * @entry: entry to sum
 */
QEMU_PLUGIN_API
uint64_t qemu_plugin_u64_sum(qemu_plugin_u64 entry);

#endif /* QEMU_QEMU_PLUGIN_H */
