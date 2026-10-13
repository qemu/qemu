/*
 * Interface between GICv5 CPU interface and GICv5 IRS
 * Loosely modelled on the GICv5 Stream Protocol interface documented
 * in the GICv5 specification.
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_STREAM_H
#define HW_INTC_ARM_GICV5_STREAM_H

#include "target/arm/cpu-qom.h"
#include "hw/intc/arm_gicv5_types.h"

typedef struct GICv5Common GICv5Common;

/**
 * gicv5_set_gicv5state
 * @cpu: CPU object to tell about its IRS
 * @cs: the GIC IRS it is connected to
 * @iaffid: the IAFFID of this CPU (guest-visible)
 * @cpuidx: the index of this CPU for this IRS (QEMU-internal)
 *
 * Set the CPU object's GICv5 pointer to point to this GIC IRS.  The
 * IRS must call this when it is realized, for each CPU it is
 * connected to.
 *
 * Returns true on success, false if the CPU doesn't implement the
 * GICv5 CPU interface.
 */
bool gicv5_set_gicv5state(ARMCPU *cpu, GICv5Common *cs, uint32_t iaffid,
                          uint32_t cpuidx);

/*
 * The architected Stream Protocol is asynchronous; commands can be
 * initiated both from the IRS and from the CPU interface, and some
 * require acknowledgement. For QEMU, we simplify this because we know
 * that in the CPU interface code we hold the BQL and so our IRS model
 * is not going to be busy; when we send commands from the CPUIF
 * ("upstream commands") we can model this as a synchronous function
 * call whose return corresponds to the acknowledgement of a completed
 * command.
 */

/**
 * gicv5_set_priority
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @priority: priority to set
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Set priority of an interrupt; matches stream interface SetPriority
 * command from CPUIF to IRS. There is no report back of
 * success/failure to the CPUIF in the protocol.
 */
void gicv5_set_priority(GICv5Common *cs, uint32_t id,
                        uint8_t priority, GICv5Domain domain,
                        GICv5IntType type, bool virtual);

/**
 * gicv5_set_enabled
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @enabled: new enabled state
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Set enabled state of an interrupt; matches stream interface
 * SetEnabled command from CPUIF to IRS. There is no report back of
 * success/failure to the CPUIF in the protocol.
 */
void gicv5_set_enabled(GICv5Common *cs, uint32_t id,
                       bool enabled, GICv5Domain domain,
                       GICv5IntType type, bool virtual);

/**
 * gicv5_set_pending
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @pending: new pending state
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Set pending state of an interrupt; matches stream interface
 * SetPending command from CPUIF to IRS. There is no report back of
 * success/failure to the CPUIF in the protocol.
 */
void gicv5_set_pending(GICv5Common *cs, uint32_t id,
                       bool pending, GICv5Domain domain,
                       GICv5IntType type, bool virtual);

/**
 * gicv5_set_handling
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @handling: new handling mode
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Set handling mode of an interrupt (edge/level); matches stream
 * interface SetHandling command from CPUIF to IRS. There is no report
 * back of success/failure to the CPUIF in the protocol.
 */
void gicv5_set_handling(GICv5Common *cs, uint32_t id,
                        GICv5HandlingMode handling, GICv5Domain domain,
                        GICv5IntType type, bool virtual);

/**
 * gicv5_set_target
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @iaffid: new target PE's interrupt affinity
 * @irm: interrupt routing mode (targeted vs 1-of-N)
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Set handling mode of an interrupt (edge/level); matches stream
 * interface SetHandling command from CPUIF to IRS. There is no report
 * back of success/failure to the CPUIF in the protocol.
 */
void gicv5_set_target(GICv5Common *cs, uint32_t id, uint32_t iaffid,
                      GICv5RoutingMode irm, GICv5Domain domain,
                      GICv5IntType type, bool virtual);

/**
 * gicv5_request_config
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @domain: interrupt domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Query the current configuration of an interrupt; matches stream
 * interface RequestConfig command from CPUIF to IRS and the
 * RequestConfigAck reply to it.
 *
 * In the real stream protocol, the RequestConfigAck packet has the
 * same information as the register but in a different order; we use
 * the register order, not the packet order, so we don't need to
 * unpack and repack in the cpuif.
 *
 * Returns: the config of the interrupt, in the format used by
 * ICC_ICSR_EL1.
 */
uint64_t gicv5_request_config(GICv5Common *cs, uint32_t id, GICv5Domain domain,
                              GICv5IntType type, bool virtual);

/**
 * gicv5_activate
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @domain: interrupt domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Activate the IRS's highest priority pending interrupt; matches the
 * stream interface's Activate command.
 *
 * In the stream interface, the command has only the domain and
 * virtual fields, because both the IRS and the CPUIF keep track of
 * the IRS's current HPPI. In QEMU, we also have arguments here for
 * @id and @type which are telling the IRS something that in hardware
 * it already knows. This is because we have them to hand in the cpuif
 * code, and it means we don't need to pass in an iaffid argument to
 * tell the IRS which CPU we are so it can find the right element in
 * its hppi[][] array.
 */
void gicv5_activate(GICv5Common *cs, uint32_t id, GICv5Domain domain,
                    GICv5IntType type, bool virtual);

/**
 * gicv5_forward_interrupt
 * @cpu: CPU interface to forward interrupt to
 * @domain: domain this interrupt is for
 *
 * Tell the CPU interface that the highest priority pending interrupt
 * that the IRS has available for it has changed.  This is the
 * equivalent of the stream protocol's Forward packet, and also of its
 * Recall packet.
 *
 * The stream protocol makes this asynchronous, allowing two Forward
 * packets to be in flight and requiring an acknowledge, because the
 * cpuif might be about to activate the previous forwarded interrupt
 * while we are trying to tell it about a new one. But for QEMU we
 * hold the BQL, so we know the vcpu might be executing guest code but
 * it cannot be in the middle of changing cpuif state. So we can just
 * synchronously tell it that a new HPPI exists (which might cause it
 * to assert IRQ or FIQ to itself); this works as if the cpuif gave us
 * a Release for the old HPPI.  The cpuif will ask the IRS for the
 * HPPI info via a function call, so we do not need to pass it across
 * here.
 */
void gicv5_forward_interrupt(ARMCPU *cpu, GICv5Domain domain);

/**
 * gicv5_get_hppi
 * @cs: GIC IRS to query
 * @domain: interrupt domain to act on
 * @iaffid: IAFFID of this CPU interface
 *
 * Ask the IRS for the highest priority pending interrupt that it has
 * for this CPU. This returns the equivalent of what in the stream
 * protocol is the outstanding interrupt sent with a Forward packet.
 */
GICv5PendingIrq gicv5_get_hppi(GICv5Common *cs, GICv5Domain domain,
                               uint32_t iaffid);

/**
 * gicv5_deactivate
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Deactivate the specified interrupt. There is no report back of
 * success/failure to the CPUIF in the protocol.
 */
void gicv5_deactivate(GICv5Common *cs, uint32_t id, GICv5Domain domain,
                      GICv5IntType type, bool virtual);

#endif
