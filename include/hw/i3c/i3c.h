/*
 * QEMU I3C bus interface.
 *
 * Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_INCLUDE_HW_I3C_I3C_H_
#define QEMU_INCLUDE_HW_I3C_I3C_H_

#include "hw/core/qdev.h"
#include "qom/object.h"
#include "hw/i2c/i2c.h"

#define TYPE_I3C_TARGET "i3c-target"
OBJECT_DECLARE_TYPE(I3CTarget, I3CTargetClass, I3C_TARGET)

typedef enum I3CEvent {
    I3C_START_RECV,
    I3C_START_SEND,
    I3C_STOP,
    I3C_NACK,
} I3CEvent;

typedef enum I3CCCC {
    /* Broadcast CCCs */
    I3C_CCC_ENEC      = 0x00,
    I3C_CCC_DISEC     = 0x01,
    I3C_CCC_ENTAS0    = 0x02,
    I3C_CCC_ENTAS1    = 0x03,
    I3C_CCC_ENTAS2    = 0x04,
    I3C_CCC_ENTAS3    = 0x05,
    I3C_CCC_RSTDAA    = 0x06,
    I3C_CCC_ENTDAA    = 0x07,
    I3C_CCC_DEFTGTS   = 0x08,
    I3C_CCC_SETMWL    = 0x09,
    I3C_CCC_SETMRL    = 0x0a,
    I3C_CCC_ENTTM     = 0x0b,
    I3C_CCC_SETBUSCON = 0x0c,
    I3C_CCC_ENDXFER   = 0x12,
    I3C_CCC_ENTHDR0   = 0x20,
    I3C_CCC_ENTHDR1   = 0x21,
    I3C_CCC_ENTHDR2   = 0x22,
    I3C_CCC_ENTHDR3   = 0x23,
    I3C_CCC_ENTHDR4   = 0x24,
    I3C_CCC_ENTHDR5   = 0x25,
    I3C_CCC_ENTHDR6   = 0x26,
    I3C_CCC_ENTHDR7   = 0x27,
    I3C_CCC_SETXTIME  = 0x28,
    I3C_CCC_SETAASA   = 0x29,
    I3C_CCC_RSTACT    = 0x2a,
    I3C_CCC_DEFGRPA   = 0x2b,
    I3C_CCC_RSTGRPA   = 0x2c,
    I3C_CCC_MLANE     = 0x2d,
    /* Direct CCCs */
    I3C_CCCD_ENEC       = 0x80,
    I3C_CCCD_DISEC      = 0x81,
    I3C_CCCD_ENTAS0     = 0x82,
    I3C_CCCD_ENTAS1     = 0x83,
    I3C_CCCD_ENTAS2     = 0x84,
    I3C_CCCD_ENTAS3     = 0x85,
    I3C_CCCD_SETDASA    = 0x87,
    I3C_CCCD_SETNEWDA   = 0x88,
    I3C_CCCD_SETMWL     = 0x89,
    I3C_CCCD_SETMRL     = 0x8a,
    I3C_CCCD_GETMWL     = 0x8b,
    I3C_CCCD_GETMRL     = 0x8c,
    I3C_CCCD_GETPID     = 0x8d,
    I3C_CCCD_GETBCR     = 0x8e,
    I3C_CCCD_GETDCR     = 0x8f,
    I3C_CCCD_GETSTATUS  = 0x90,
    I3C_CCCD_GETACCCR   = 0x91,
    I3C_CCCD_ENDXFER    = 0x92,
    I3C_CCCD_SETBRGTGT  = 0x93,
    I3C_CCCD_GETMXDS    = 0x94,
    I3C_CCCD_GETCAPS    = 0x95,
    I3C_CCCD_SETROUTE   = 0x96,
    I3C_CCCD_SETXTIME   = 0x98,
    I3C_CCCD_GETXTIME   = 0x99,
    I3C_CCCD_RSTACT     = 0x9a,
    I3C_CCCD_SETGRPA    = 0x9b,
    I3C_CCCD_RSTGRPA    = 0x9c,
    I3C_CCCD_MLANE      = 0x9d,
} I3CCCC;

#define CCC_IS_DIRECT(_ccc) (_ccc & 0x80)

#define I3C_BROADCAST 0x7e
#define I3C_HJ_ADDR 0x02
#define I3C_ENTDAA_SIZE 8

struct I3CTargetClass {
    DeviceClass parent_class;

    /*
     * Controller to target. Returns 0 for success, non-zero for NAK or other
     * error.
     */
    int (*send)(I3CTarget *s, const uint8_t *data, uint32_t num_to_send,
                uint32_t *num_sent);
    /*
     * Target to controller. I3C targets are able to terminate reads early, so
     * this returns the number of bytes read from the target.
     */
    uint32_t (*recv)(I3CTarget *s, uint8_t *data, uint32_t num_to_read);
    /* Notify the target of a bus state change. */
    int (*event)(I3CTarget *s, enum I3CEvent event);
    /*
     * Handle a read CCC transmitted from a controller.
     * CCCs are I3C commands that I3C targets support.
     * The target can NACK the CCC if it does not support it.
     */
    int (*handle_ccc_read)(I3CTarget *s, uint8_t *data, uint32_t num_to_read,
                           uint32_t *num_read);
    /*
     * Handle a write CCC transmitted from a controller.
     * CCCs are I3C commands that I3C targets support.
     * The target can NACK the CCC if it does not support it.
     */
    int (*handle_ccc_write)(I3CTarget *s, const uint8_t *data,
                            uint32_t num_to_send, uint32_t *num_sent);

    /*
     * Matches and adds the candidate if the address matches the candidate's
     * address.
     * Returns true if the address matched, or if this was a broadcast, and
     * updates the device list. Otherwise returns false.
     */
    bool (*target_match)(I3CTarget *candidate, uint8_t address, bool is_read,
                         bool broadcast, bool in_entdaa);
};

struct I3CTarget {
    DeviceState parent_obj;

    uint8_t address;
    uint8_t static_address;
    uint8_t dcr;
    uint8_t bcr;
    uint64_t pid;

    /* CCC State tracking. */
    I3CCCC curr_ccc;
    uint8_t ccc_byte_offset;
    bool in_ccc;
    bool in_test_mode;
};

struct I3CNode {
    I3CTarget *target;
    QLIST_ENTRY(I3CNode) next;
};

typedef struct I3CNode I3CNode;

typedef QLIST_HEAD(I3CNodeList, I3CNode) I3CNodeList;

#define TYPE_I3C_BUS "i3c-bus"
OBJECT_DECLARE_TYPE(I3CBus, I3CBusClass, I3C_BUS)

struct I3CBus {
    BusState parent_obj;

    /* Legacy I2C. */
    I2CBus *i2c_bus;

    I3CNodeList current_devs;
    bool broadcast;
    uint8_t ccc;
    bool in_ccc;
    bool in_entdaa;
    uint8_t saved_address;
};

struct I3CBusClass {
    BusClass parent_class;

    /* Handle an incoming IBI request from a target */
    int (*ibi_handle) (I3CBus *bus, uint8_t addr, bool is_recv);
    /* Receive data from an IBI request */
    int (*ibi_recv) (I3CBus *bus, uint8_t data);
    /* Do anything that needs to be done, since the IBI is finished. */
    int (*ibi_finish) (I3CBus *bus);
};

I3CBus *i3c_init_bus(DeviceState *parent, const char *name);
I3CBus *i3c_init_bus_type(const char *type, DeviceState *parent,
                          const char *name);
void i3c_set_target_address(I3CTarget *dev, uint8_t address);
bool i3c_bus_busy(I3CBus *bus);

/*
 * Start a transfer on an I3C bus.
 * If is_recv is known at compile-time (i.e. a device will always be sending or
 * will always be receiving at a certain point), prefer to use i3c_start_recv or
 * i3c_start_send instead.
 *
 * Returns 0 on success, non-zero on an error.
 */
int i3c_start_transfer(I3CBus *bus, uint8_t address, bool is_recv);

/*
 * Start a receive transfer on an I3C bus.
 *
 * Returns 0 on success, non-zero on an error
 */
int i3c_start_recv(I3CBus *bus, uint8_t address);

/*
 * Start a send transfer on an I3C bus.
 *
 * Returns 0 on success, non-zero on an error
 */
int i3c_start_send(I3CBus *bus, uint8_t address);

void i3c_end_transfer(I3CBus *bus);
void i3c_nack(I3CBus *bus);
int i3c_send_byte(I3CBus *bus, uint8_t data);
int i3c_send(I3CBus *bus, const uint8_t *data, uint32_t num_to_send,
             uint32_t *num_sent);
/*
 * I3C receives can only NACK on a CCC. The target should NACK a CCC it does not
 * support.
 */
int i3c_recv_byte(I3CBus *bus, uint8_t *data);
int i3c_recv(I3CBus *bus, uint8_t *data, uint32_t num_to_read,
             uint32_t *num_read);
bool i3c_scan_bus(I3CBus *bus, uint8_t address, enum I3CEvent event);
int i3c_do_entdaa(I3CBus *bus, uint8_t address, uint64_t *pid, uint8_t *bcr,
                  uint8_t *dcr);
int i3c_start_device_transfer(I3CTarget *dev, int send_length);
bool i3c_target_match_and_add(I3CBus *bus, I3CTarget *target, uint8_t address,
                             enum I3CEvent event);
int i3c_target_send_ibi(I3CTarget *t, uint8_t addr, bool is_recv);
int i3c_target_send_ibi_bytes(I3CTarget *t, uint8_t data);
int i3c_target_ibi_finish(I3CTarget *t, uint8_t data);

/*
 * Legacy I2C functions.
 *
 * These are wrapper for I2C functions that take in an I3C bus instead of an I2C
 * bus. Internally they use the I2C bus (and devices attached to it) that's a
 * part of the I3C bus
 */
void legacy_i2c_nack(I3CBus *bus);
uint8_t legacy_i2c_recv(I3CBus *bus);
int legacy_i2c_send(I3CBus *bus, uint8_t data);
int legacy_i2c_start_transfer(I3CBus *bus, uint8_t address, bool is_recv);
int legacy_i2c_start_recv(I3CBus *bus, uint8_t address);
int legacy_i2c_start_send(I3CBus *bus, uint8_t address);
void legacy_i2c_end_transfer(I3CBus *bus);
I2CSlave *legacy_i2c_device_create_simple(I3CBus *bus, const char *name,
                                          uint8_t addr);

/**
 * Create an I3C Target.
 *
 * The target returned from this function still needs to be realized.
 */
I3CTarget *i3c_target_new(const char *name, uint8_t addr, uint8_t dcr,
                          uint8_t bcr, uint64_t pid);

/**
 * Create and realize an I3C target.
 *
 * Create the target, initialize it, put it on the specified I3C bus, and
 * realize it.
 */
I3CTarget *i3c_target_create_simple(I3CBus *bus, const char *name,
                                    uint8_t addr, uint8_t dcr, uint8_t bcr,
                                    uint64_t pid);

/* Realize and drop the reference count on an I3C target. */
bool i3c_target_realize_and_unref(I3CTarget *dev, I3CBus *bus, Error **errp);

#endif  /* QEMU_INCLUDE_HW_I3C_I3C_H_ */
