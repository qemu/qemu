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
};

/* Instantiated Wasm function of a TB */
typedef int32_t (*wasm_tb_func)(struct wasmContext *);

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
};

#endif
