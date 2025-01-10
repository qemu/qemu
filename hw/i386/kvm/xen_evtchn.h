/*
 * QEMU Xen emulation: Event channel support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_EVTCHN_H
#define QEMU_XEN_EVTCHN_H

#include "hw/sysbus.h"

typedef uint32_t evtchn_port_t;

void xen_evtchn_create(unsigned int nr_gsis, qemu_irq *system_gsis);
int xen_evtchn_soft_reset(void);
int xen_evtchn_set_callback_param(uint64_t param);
void xen_evtchn_set_callback_level(int level);

int xen_evtchn_set_port(uint16_t port);

bool xen_evtchn_set_gsi(int gsi, int *level);
void xen_evtchn_snoop_msi(PCIDevice *dev, bool is_msix, unsigned int vector,
                          uint64_t addr, uint32_t data, bool is_masked);
void xen_evtchn_remove_pci_device(PCIDevice *dev);
struct kvm_irq_routing_entry;
int xen_evtchn_translate_pirq_msi(struct kvm_irq_routing_entry *route,
                                  uint64_t address, uint32_t data);
bool xen_evtchn_deliver_pirq_msi(uint64_t address, uint32_t data);


/*
 * These functions mirror the libxenevtchn library API, providing the QEMU
 * backend side of "interdomain" event channels.
 */
struct xenevtchn_handle;
struct xenevtchn_handle *xen_be_evtchn_open(void);
int xen_be_evtchn_bind_interdomain(struct xenevtchn_handle *xc, uint32_t domid,
                                   evtchn_port_t guest_port);
int xen_be_evtchn_unbind(struct xenevtchn_handle *xc, evtchn_port_t port);
int xen_be_evtchn_close(struct xenevtchn_handle *xc);
int xen_be_evtchn_fd(struct xenevtchn_handle *xc);
int xen_be_evtchn_notify(struct xenevtchn_handle *xc, evtchn_port_t port);
int xen_be_evtchn_unmask(struct xenevtchn_handle *xc, evtchn_port_t port);
int xen_be_evtchn_pending(struct xenevtchn_handle *xc);
/* Apart from this which is a local addition */
int xen_be_evtchn_get_guest_port(struct xenevtchn_handle *xc);

struct evtchn_status;
struct evtchn_close;
struct evtchn_unmask;
struct evtchn_bind_virq;
struct evtchn_bind_pirq;
struct evtchn_bind_ipi;
struct evtchn_send;
struct evtchn_alloc_unbound;
struct evtchn_bind_interdomain;
struct evtchn_bind_vcpu;
struct evtchn_reset;
int xen_evtchn_status_op(struct evtchn_status *status);
int xen_evtchn_close_op(struct evtchn_close *close);
int xen_evtchn_unmask_op(struct evtchn_unmask *unmask);
int xen_evtchn_bind_virq_op(struct evtchn_bind_virq *virq);
int xen_evtchn_bind_pirq_op(struct evtchn_bind_pirq *pirq);
int xen_evtchn_bind_ipi_op(struct evtchn_bind_ipi *ipi);
int xen_evtchn_send_op(struct evtchn_send *send);
int xen_evtchn_alloc_unbound_op(struct evtchn_alloc_unbound *alloc);
int xen_evtchn_bind_interdomain_op(struct evtchn_bind_interdomain *interdomain);
int xen_evtchn_bind_vcpu_op(struct evtchn_bind_vcpu *vcpu);
int xen_evtchn_reset_op(struct evtchn_reset *reset);

struct physdev_map_pirq;
struct physdev_unmap_pirq;
struct physdev_eoi;
struct physdev_irq_status_query;
struct physdev_get_free_pirq;
int xen_physdev_map_pirq(struct physdev_map_pirq *map);
int xen_physdev_unmap_pirq(struct physdev_unmap_pirq *unmap);
int xen_physdev_eoi_pirq(struct physdev_eoi *eoi);
int xen_physdev_query_pirq(struct physdev_irq_status_query *query);
int xen_physdev_get_free_pirq(struct physdev_get_free_pirq *get);

#endif /* QEMU_XEN_EVTCHN_H */
