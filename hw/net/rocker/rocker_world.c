/*
 * QEMU rocker switch emulation - switch worlds
 *
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"

#include "rocker.h"
#include "rocker_world.h"

struct world {
    Rocker *r;
    enum rocker_world_type type;
    WorldOps *ops;
};

ssize_t world_ingress(World *world, uint32_t pport,
                      const struct iovec *iov, int iovcnt)
{
    if (world->ops->ig) {
        return world->ops->ig(world, pport, iov, iovcnt);
    }

    return -1;
}

int world_do_cmd(World *world, DescInfo *info,
                 char *buf, uint16_t cmd, RockerTlv *cmd_info_tlv)
{
    if (world->ops->cmd) {
        return world->ops->cmd(world, info, buf, cmd, cmd_info_tlv);
    }

    return -ROCKER_ENOTSUP;
}

World *world_alloc(Rocker *r, size_t sizeof_private,
                   enum rocker_world_type type, WorldOps *ops)
{
    World *w = g_malloc0(sizeof(World) + sizeof_private);

    w->r = r;
    w->type = type;
    w->ops = ops;
    if (w->ops->init) {
        w->ops->init(w);
    }

    return w;
}

void world_free(World *world)
{
    if (world->ops->uninit) {
        world->ops->uninit(world);
    }
    g_free(world);
}

void world_reset(World *world)
{
    if (world->ops->uninit) {
        world->ops->uninit(world);
    }
    if (world->ops->init) {
        world->ops->init(world);
    }
}

void *world_private(World *world)
{
    return world + 1;
}

Rocker *world_rocker(World *world)
{
    return world->r;
}

enum rocker_world_type world_type(World *world)
{
    return world->type;
}

const char *world_name(World *world)
{
    return world->ops->name;
}
