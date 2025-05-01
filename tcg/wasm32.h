/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TCG_WASM32_H
#define TCG_WASM32_H

/*
 * wasmContext is a data shared among QEMU and wasm modules.
 */
struct wasmContext {
    /*
     * Pointer to the TB to be executed.
     */
    void *tb_ptr;

    /*
     * Pointer to the tci_tb_ptr variable.
     */
    void *tci_tb_ptr;

    /*
     * Buffer to store 128bit return value on call.
     */
    void *buf128;

    /*
     * Pointer to CPUArchState struct.
     */
    CPUArchState *env;

    /*
     * Pointer to a stack array.
     */
    uint64_t *stack;

    /*
     * Flag indicates whether to initialize basic registers(1) or not(0).
     */
    uint32_t do_init;
};

/* Instantiated Wasm function of a TB */
typedef int32_t (*wasm_tb_func)(struct wasmContext *);

static inline int32_t call_wasm_tb(wasm_tb_func f, struct wasmContext *ctx)
{
    ctx->do_init = 1; /* reset block index (rewinding will skip this) */
    return f(ctx);
}

/*
 * wasmInstanceInfo holds the relationship between TB and Wasm instance.
 */
struct wasmInstanceInfo {
    void *tb_ptr;
    wasm_tb_func tb_func;
};

/*
 * TB of wasm backend starts from a header which stores pointers for each data
 * stored in the following region in the TB.
 */
struct wasmTBHeader {
    /*
     * Pointer to the region containing TCI instructions.
     */
    void *tci_ptr;

    /*
     * Pointer to the region containing Wasm instructions.
     */
    void *wasm_ptr;
    int wasm_size;

    /*
     * Pointer to the array containing imported function pointers.
     */
    void *import_ptr;
    int import_size;

    /*
     * Counter holds how many times the TB is executed before instantiation
     * for each thread.
     */
    int32_t *counter_ptr;

    /*
     * Pointer to the instance information on each thread.
     */
    struct wasmInstanceInfo **info_ptr;
};

static inline uint32_t *get_tci_ptr(void *tb_ptr)
{
    return (uint32_t *)(((struct wasmTBHeader *)tb_ptr)->tci_ptr);
}

static inline int32_t get_counter(void *tb_ptr, int idx)
{
    return ((struct wasmTBHeader *)tb_ptr)->counter_ptr[idx];
}

static inline void set_counter(void *tb_ptr, int idx, int v)
{
    ((struct wasmTBHeader *)tb_ptr)->counter_ptr[idx] = v;
}

static inline struct wasmInstanceInfo *get_info(void *tb_ptr, int idx)
{
    return ((struct wasmTBHeader *)tb_ptr)->info_ptr[idx];
}

static inline void set_info(void *tb_ptr, int idx,
                            struct wasmInstanceInfo *info)
{
    ((struct wasmTBHeader *)tb_ptr)->info_ptr[idx] = info;
}

#endif
