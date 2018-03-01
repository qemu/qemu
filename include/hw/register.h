/*
 * Register Definition API
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef REGISTER_H
#define REGISTER_H

#include "hw/qdev-core.h"
#include "exec/memory.h"
#include "hw/registerfields.h"

typedef struct RegisterInfo RegisterInfo;
typedef struct RegisterAccessInfo RegisterAccessInfo;
typedef struct RegisterInfoArray RegisterInfoArray;

/**
 * Access description for a register that is part of guest accessible device
 * state.
 *
 * @name: String name of the register
 * @ro: whether or not the bit is read-only
 * @w1c: bits with the common write 1 to clear semantic.
 * @reset: reset value.
 * @cor: Bits that are clear on read
 * @rsvd: Bits that are reserved and should not be changed
 *
 * @pre_write: Pre write callback. Passed the value that's to be written,
 * immediately before the actual write. The returned value is what is written,
 * giving the handler a chance to modify the written value.
 * @post_write: Post write callback. Passed the written value. Most write side
 * effects should be implemented here. This is called during device reset.
 *
 * @post_read: Post read callback. Passes the value that is about to be returned
 * for a read. The return value from this function is what is ultimately read,
 * allowing this function to modify the value before return to the client.
 */

struct RegisterAccessInfo {
    const char *name;
    uint64_t ro;
    uint64_t w1c;
    uint64_t reset;
    uint64_t cor;
    uint64_t rsvd;
    uint64_t unimp;

    uint64_t (*pre_write)(RegisterInfo *reg, uint64_t val);
    void (*post_write)(RegisterInfo *reg, uint64_t val);

    uint64_t (*post_read)(RegisterInfo *reg, uint64_t val);

    hwaddr addr;
};

/**
 * A register that is part of guest accessible state
 * @data: pointer to the register data. Will be cast
 * to the relevant uint type depending on data_size.
 * @data_size: Size of the register in bytes. Must be
 * 1, 2, 4 or 8
 *
 * @access: Access description of this register
 *
 * @debug: Whether or not verbose debug is enabled
 * @prefix: String prefix for log and debug messages
 *
 * @opaque: Opaque data for the register
 */

struct RegisterInfo {
    /* <private> */
    DeviceState parent_obj;

    /* <public> */
    void *data;
    int data_size;

    const RegisterAccessInfo *access;

    void *opaque;
};

#define TYPE_REGISTER "qemu,register"
#define REGISTER(obj) OBJECT_CHECK(RegisterInfo, (obj), TYPE_REGISTER)

/**
 * This structure is used to group all of the individual registers which are
 * modeled using the RegisterInfo structure.
 *
 * @r is an array containing of all the relevant RegisterInfo structures.
 *
 * @num_elements is the number of elements in the array r
 *
 * @mem: optional Memory region for the register
 */

struct RegisterInfoArray {
    MemoryRegion mem;

    int num_elements;
    RegisterInfo **r;

    bool debug;
    const char *prefix;
};

/**
 * write a value to a register, subject to its restrictions
 * @reg: register to write to
 * @val: value to write
 * @we: write enable mask
 * @prefix: The device prefix that should be printed before the register name
 * @debug: Should the write operation debug information be printed?
 */

void register_write(RegisterInfo *reg, uint64_t val, uint64_t we,
                    const char *prefix, bool debug);

/**
 * read a value from a register, subject to its restrictions
 * @reg: register to read from
 * @re: read enable mask
 * @prefix: The device prefix that should be printed before the register name
 * @debug: Should the read operation debug information be printed?
 * returns: value read
 */

uint64_t register_read(RegisterInfo *reg, uint64_t re, const char* prefix,
                       bool debug);

/**
 * Resets a register. This will also call the post_write hook if it exists.
 * @reg: The register to reset.
 */

void register_reset(RegisterInfo *reg);

/**
 * Initialize a register.
 * @reg: Register to initialize
 */

void register_init(RegisterInfo *reg);

/**
 * Memory API MMIO write handler that will write to a Register API register.
 * @opaque: RegisterInfo to write to
 * @addr: Address to write
 * @value: Value to write
 * @size: Number of bytes to write
 */

void register_write_memory(void *opaque, hwaddr addr, uint64_t value,
                           unsigned size);

/**
 * Memory API MMIO read handler that will read from a Register API register.
 * @opaque: RegisterInfo to read from
 * @addr: Address to read
 * @size: Number of bytes to read
 * returns: Value read from register
 */

uint64_t register_read_memory(void *opaque, hwaddr addr, unsigned size);

/**
 * Init a block of registers into a container MemoryRegion. A
 * number of constant register definitions are parsed to create a corresponding
 * array of RegisterInfo's.
 *
 * @owner: device owning the registers
 * @rae: Register definitions to init
 * @num: number of registers to init (length of @rae)
 * @ri: Register array to init, must already be allocated
 * @data: Array to use for register data, must already be allocated
 * @ops: Memory region ops to access registers.
 * @debug enabled: turn on/off verbose debug information
 * returns: A structure containing all of the registers and an initialized
 *          memory region (r_array->mem) the caller should add to a container.
 */

RegisterInfoArray *register_init_block32(DeviceState *owner,
                                         const RegisterAccessInfo *rae,
                                         int num, RegisterInfo *ri,
                                         uint32_t *data,
                                         const MemoryRegionOps *ops,
                                         bool debug_enabled,
                                         uint64_t memory_size);

/**
 * This function should be called to cleanup the registers that were initialized
 * when calling register_init_block32(). This function should only be called
 * from the device's instance_finalize function.
 *
 * Any memory operations that the device performed that require cleanup (such
 * as creating subregions) need to be called before calling this function.
 *
 * @r_array: A structure containing all of the registers, as returned by
 *           register_init_block32()
 */

void register_finalize_block(RegisterInfoArray *r_array);

#endif
