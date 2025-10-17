/*
 * Register Definition API
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "hw/register.h"
#include "qemu/log.h"
#include "qemu/module.h"

static inline void register_write_val(RegisterInfo *reg, uint64_t val)
{
    g_assert(reg->data);

    switch (reg->data_size) {
    case 1:
        *(uint8_t *)reg->data = val;
        break;
    case 2:
        *(uint16_t *)reg->data = val;
        break;
    case 4:
        *(uint32_t *)reg->data = val;
        break;
    case 8:
        *(uint64_t *)reg->data = val;
        break;
    default:
        g_assert_not_reached();
    }
}

static inline uint64_t register_read_val(RegisterInfo *reg)
{
    switch (reg->data_size) {
    case 1:
        return *(uint8_t *)reg->data;
    case 2:
        return *(uint16_t *)reg->data;
    case 4:
        return *(uint32_t *)reg->data;
    case 8:
        return *(uint64_t *)reg->data;
    default:
        g_assert_not_reached();
    }
    return 0; /* unreachable */
}

static inline uint64_t register_enabled_mask(int data_size, unsigned size)
{
    if (data_size < size) {
        size = data_size;
    }

    return MAKE_64BIT_MASK(0, size * 8);
}

void register_write(RegisterInfo *reg, uint64_t val, uint64_t we,
                    const char *prefix, bool debug)
{
    uint64_t old_val, new_val, test, no_w_mask;
    const RegisterAccessInfo *ac;

    assert(reg);

    ac = reg->access;

    if (!ac || !ac->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to undefined device state "
                      "(written value: 0x%" PRIx64 ")\n", prefix, val);
        return;
    }

    old_val = reg->data ? register_read_val(reg) : ac->reset;

    test = (old_val ^ val) & ac->rsvd;
    if (test) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: change of value in reserved bit"
                      "fields: 0x%" PRIx64 ")\n", prefix, test);
    }

    test = val & ac->unimp;
    if (test) {
        qemu_log_mask(LOG_UNIMP,
                      "%s:%s writing 0x%" PRIx64 " to unimplemented bits:" \
                      " 0x%" PRIx64 "\n",
                      prefix, reg->access->name, val, ac->unimp);
    }

    /* Create the no write mask based on the read only, write to clear and
     * reserved bit masks.
     */
    no_w_mask = ac->ro | ac->w1c | ac->rsvd | ~we;
    new_val = (val & ~no_w_mask) | (old_val & no_w_mask);
    new_val &= ~(val & ac->w1c);

    if (ac->pre_write) {
        new_val = ac->pre_write(reg, new_val);
    }

    if (debug) {
        qemu_log("%s:%s: write of value 0x%" PRIx64 "\n", prefix, ac->name,
                 new_val);
    }

    register_write_val(reg, new_val);

    if (ac->post_write) {
        ac->post_write(reg, new_val);
    }
}

uint64_t register_read(RegisterInfo *reg, uint64_t re, const char* prefix,
                       bool debug)
{
    uint64_t ret;
    const RegisterAccessInfo *ac;

    assert(reg);

    ac = reg->access;
    if (!ac || !ac->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from undefined device state\n",
                      prefix);
        return 0;
    }

    ret = reg->data ? register_read_val(reg) : ac->reset;

    register_write_val(reg, ret & ~(ac->cor & re));

    /* Mask based on the read enable size */
    ret &= re;

    if (ac->post_read) {
        ret = ac->post_read(reg, ret);
    }

    if (debug) {
        qemu_log("%s:%s: read of value 0x%" PRIx64 "\n", prefix,
                 ac->name, ret);
    }

    return ret;
}

void register_reset(RegisterInfo *reg)
{
    const RegisterAccessInfo *ac;

    g_assert(reg);

    if (!reg->data || !reg->access) {
        return;
    }

    ac = reg->access;

    register_write_val(reg, reg->access->reset);

    if (ac->post_write) {
        ac->post_write(reg, reg->access->reset);
    }
}

void register_write_memory(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    RegisterInfo *reg = NULL;
    uint64_t we;
    int i;

    for (i = 0; i < reg_array->num_elements; i++) {
        if (reg_array->r[i]->access->addr == addr) {
            reg = reg_array->r[i];
            break;
        }
    }

    if (!reg) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unimplemented register " \
                      "at address: 0x%" PRIx64 "\n", reg_array->prefix, addr);
        return;
    }

    /* Generate appropriate write enable mask */
    we = register_enabled_mask(reg->data_size, size);

    register_write(reg, value, we, reg_array->prefix,
                   reg_array->debug);
}

uint64_t register_read_memory(void *opaque, hwaddr addr,
                              unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    RegisterInfo *reg = NULL;
    uint64_t read_val;
    uint64_t re;
    int i;

    for (i = 0; i < reg_array->num_elements; i++) {
        if (reg_array->r[i]->access->addr == addr) {
            reg = reg_array->r[i];
            break;
        }
    }

    if (!reg) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s:  read to unimplemented register " \
                      "at address: 0x%" PRIx64 "\n", reg_array->prefix, addr);
        return 0;
    }

    /* Generate appropriate read enable mask */
    re = register_enabled_mask(reg->data_size, size);

    read_val = register_read(reg, re, reg_array->prefix,
                             reg_array->debug);

    return extract64(read_val, 0, size * 8);
}

static RegisterInfoArray *register_init_block(DeviceState *owner,
                                              const RegisterAccessInfo *rae,
                                              int num, RegisterInfo *ri,
                                              void *data,
                                              const MemoryRegionOps *ops,
                                              bool debug_enabled,
                                              uint64_t memory_size,
                                              size_t data_size_bits)
{
    const char *device_prefix = object_get_typename(OBJECT(owner));
    Object *obj;
    RegisterInfoArray *r_array;
    int data_size = data_size_bits >> 3;
    int i;

    obj = object_new(TYPE_REGISTER_ARRAY);
    object_property_add_child(OBJECT(owner), "reg-array[*]", obj);
    object_unref(obj);

    r_array = REGISTER_ARRAY(obj);
    r_array->r = g_new0(RegisterInfo *, num);
    r_array->num_elements = num;
    r_array->debug = debug_enabled;
    r_array->prefix = device_prefix;

    for (i = 0; i < num; i++) {
        int index = rae[i].addr / data_size;
        RegisterInfo *r = &ri[index];

        /* Set the properties of the register */
        r->data = data + data_size * index;
        r->data_size = data_size;
        r->access = &rae[i];
        r->opaque = owner;

        r_array->r[i] = r;
    }

    memory_region_init_io(&r_array->mem, OBJECT(r_array), ops, r_array,
                          device_prefix, memory_size);

    return r_array;
}

RegisterInfoArray *register_init_block8(DeviceState *owner,
                                        const RegisterAccessInfo *rae,
                                        int num, RegisterInfo *ri,
                                        uint8_t *data,
                                        const MemoryRegionOps *ops,
                                        bool debug_enabled,
                                        uint64_t memory_size)
{
    return register_init_block(owner, rae, num, ri, (void *)
                               data, ops, debug_enabled, memory_size, 8);
}

RegisterInfoArray *register_init_block32(DeviceState *owner,
                                         const RegisterAccessInfo *rae,
                                         int num, RegisterInfo *ri,
                                         uint32_t *data,
                                         const MemoryRegionOps *ops,
                                         bool debug_enabled,
                                         uint64_t memory_size)
{
    return register_init_block(owner, rae, num, ri, (void *)
                               data, ops, debug_enabled, memory_size, 32);
}

RegisterInfoArray *register_init_block64(DeviceState *owner,
                                         const RegisterAccessInfo *rae,
                                         int num, RegisterInfo *ri,
                                         uint64_t *data,
                                         const MemoryRegionOps *ops,
                                         bool debug_enabled,
                                         uint64_t memory_size)
{
    return register_init_block(owner, rae, num, ri, (void *)
                               data, ops, debug_enabled, memory_size, 64);
}

static void register_array_finalize(Object *obj)
{
    RegisterInfoArray *r_array = REGISTER_ARRAY(obj);

    g_free(r_array->r);
}

static const TypeInfo register_array_info = {
    .name  = TYPE_REGISTER_ARRAY,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RegisterInfoArray),
    .instance_finalize = register_array_finalize,
};

static void register_register_types(void)
{
    type_register_static(&register_array_info);
}

type_init(register_register_types)
