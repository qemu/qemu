/*
 * Generic Loader
 *
 * Copyright (C) 2014 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
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

#ifndef GENERIC_LOADER_H
#define GENERIC_LOADER_H

#include "elf.h"

typedef struct GenericLoaderState {
    /* <private> */
    DeviceState parent_obj;

    /* <public> */
    CPUState *cpu;

    uint64_t addr;
    uint64_t data;
    uint8_t data_len;
    uint32_t cpu_num;

    char *file;

    bool force_raw;
    bool data_be;
    bool set_pc;
} GenericLoaderState;

#define TYPE_GENERIC_LOADER "loader"
#define GENERIC_LOADER(obj) OBJECT_CHECK(GenericLoaderState, (obj), \
                                         TYPE_GENERIC_LOADER)

#endif
