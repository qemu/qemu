/*
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PROXY_MEMORY_LISTENER_H
#define PROXY_MEMORY_LISTENER_H

#include "system/memory.h"
#include "io/channel.h"

typedef struct ProxyMemoryListener {
    MemoryListener listener;

    int n_mr_sections;
    MemoryRegionSection *mr_sections;

    QIOChannel *ioc;
} ProxyMemoryListener;

void proxy_memory_listener_configure(ProxyMemoryListener *proxy_listener,
                                     QIOChannel *ioc);
void proxy_memory_listener_deconfigure(ProxyMemoryListener *proxy_listener);

#endif
