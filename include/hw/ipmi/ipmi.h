/*
 * IPMI base class
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_IPMI_H
#define HW_IPMI_H

#include "exec/memory.h"
#include "hw/qdev-core.h"

#define MAX_IPMI_MSG_SIZE 300

enum ipmi_op {
    IPMI_RESET_CHASSIS,
    IPMI_POWEROFF_CHASSIS,
    IPMI_POWERON_CHASSIS,
    IPMI_POWERCYCLE_CHASSIS,
    IPMI_PULSE_DIAG_IRQ,
    IPMI_SHUTDOWN_VIA_ACPI_OVERTEMP,
    IPMI_SEND_NMI
};

#define IPMI_CC_INVALID_CMD                              0xc1
#define IPMI_CC_COMMAND_INVALID_FOR_LUN                  0xc2
#define IPMI_CC_TIMEOUT                                  0xc3
#define IPMI_CC_OUT_OF_SPACE                             0xc4
#define IPMI_CC_INVALID_RESERVATION                      0xc5
#define IPMI_CC_REQUEST_DATA_TRUNCATED                   0xc6
#define IPMI_CC_REQUEST_DATA_LENGTH_INVALID              0xc7
#define IPMI_CC_PARM_OUT_OF_RANGE                        0xc9
#define IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES              0xca
#define IPMI_CC_REQ_ENTRY_NOT_PRESENT                    0xcb
#define IPMI_CC_INVALID_DATA_FIELD                       0xcc
#define IPMI_CC_BMC_INIT_IN_PROGRESS                     0xd2
#define IPMI_CC_COMMAND_NOT_SUPPORTED                    0xd5

#define IPMI_NETFN_APP                0x06

#define IPMI_DEBUG 1

/* Specified in the SMBIOS spec. */
#define IPMI_SMBIOS_KCS         0x01
#define IPMI_SMBIOS_SMIC        0x02
#define IPMI_SMBIOS_BT          0x03
#define IPMI_SMBIOS_SSIF        0x04

/*
 * Used for transferring information to interfaces that add
 * entries to firmware tables.
 */
typedef struct IPMIFwInfo {
    const char *interface_name;
    int interface_type;
    uint8_t ipmi_spec_major_revision;
    uint8_t ipmi_spec_minor_revision;
    uint8_t i2c_slave_address;
    uint32_t uuid;

    uint64_t base_address;
    uint64_t register_length;
    uint8_t register_spacing;
    enum {
        IPMI_MEMSPACE_IO,
        IPMI_MEMSPACE_MEM32,
        IPMI_MEMSPACE_MEM64,
        IPMI_MEMSPACE_SMBUS
    } memspace;

    int interrupt_number;
    enum {
        IPMI_LEVEL_IRQ,
        IPMI_EDGE_IRQ
    } irq_type;
} IPMIFwInfo;

/*
 * Called by each instantiated IPMI interface device to get it's uuid.
 */
uint32_t ipmi_next_uuid(void);

/* IPMI Interface types (KCS, SMIC, BT) are prefixed with this */
#define TYPE_IPMI_INTERFACE_PREFIX "ipmi-interface-"

/*
 * An IPMI Interface, the interface for talking between the target
 * and the BMC.
 */
#define TYPE_IPMI_INTERFACE "ipmi-interface"
#define IPMI_INTERFACE(obj) \
     INTERFACE_CHECK(IPMIInterface, (obj), TYPE_IPMI_INTERFACE)
#define IPMI_INTERFACE_CLASS(class) \
     OBJECT_CLASS_CHECK(IPMIInterfaceClass, (class), TYPE_IPMI_INTERFACE)
#define IPMI_INTERFACE_GET_CLASS(class) \
     OBJECT_GET_CLASS(IPMIInterfaceClass, (class), TYPE_IPMI_INTERFACE)

typedef struct IPMIInterface IPMIInterface;

typedef struct IPMIInterfaceClass {
    InterfaceClass parent;

    /*
     * min_size is the requested I/O size and must be a power of 2.
     * This is so PCI (or other busses) can request a bigger range.
     * Use 0 for the default.
     */
    void (*init)(struct IPMIInterface *s, unsigned int min_size, Error **errp);

    /*
     * Perform various operations on the hardware.  If checkonly is
     * true, it will return if the operation can be performed, but it
     * will not do the operation.
     */
    int (*do_hw_op)(struct IPMIInterface *s, enum ipmi_op op, int checkonly);

    /*
     * Enable/disable irqs on the interface when the BMC requests this.
     */
    void (*set_irq_enable)(struct IPMIInterface *s, int val);

    /*
     * Handle an event that occurred on the interface, generally the.
     * target writing to a register.
     */
    void (*handle_if_event)(struct IPMIInterface *s);

    /*
     * The interfaces use this to perform certain ops
     */
    void (*set_atn)(struct IPMIInterface *s, int val, int irq);

    /*
     * Got an IPMI warm/cold reset.
     */
    void (*reset)(struct IPMIInterface *s, bool is_cold);

    /*
     * Handle a response from the bmc.
     */
    void (*handle_rsp)(struct IPMIInterface *s, uint8_t msg_id,
                       unsigned char *rsp, unsigned int rsp_len);

    /*
     * Set by the owner to hold the backend data for the interface.
     */
    void *(*get_backend_data)(struct IPMIInterface *s);

    /*
     * Return the firmware info for a device.
     */
    void (*get_fwinfo)(struct IPMIInterface *s, IPMIFwInfo *info);
} IPMIInterfaceClass;

/*
 * Define a BMC simulator (or perhaps a connection to a real BMC)
 */
#define TYPE_IPMI_BMC "ipmi-bmc"
#define IPMI_BMC(obj) \
     OBJECT_CHECK(IPMIBmc, (obj), TYPE_IPMI_BMC)
#define IPMI_BMC_CLASS(obj_class) \
     OBJECT_CLASS_CHECK(IPMIBmcClass, (obj_class), TYPE_IPMI_BMC)
#define IPMI_BMC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(IPMIBmcClass, (obj), TYPE_IPMI_BMC)

typedef struct IPMIBmc {
    DeviceState parent;

    uint8_t slave_addr;

    IPMIInterface *intf;
} IPMIBmc;

typedef struct IPMIBmcClass {
    DeviceClass parent;

    /* Called when the system resets to report to the bmc. */
    void (*handle_reset)(struct IPMIBmc *s);

    /*
     * Handle a command to the bmc.
     */
    void (*handle_command)(struct IPMIBmc *s,
                           uint8_t *cmd, unsigned int cmd_len,
                           unsigned int max_cmd_len,
                           uint8_t msg_id);
} IPMIBmcClass;

/*
 * Add a link property to obj that points to a BMC.
 */
void ipmi_bmc_find_and_link(Object *obj, Object **bmc);

#ifdef IPMI_DEBUG
#define ipmi_debug(fs, ...) \
    fprintf(stderr, "IPMI (%s): " fs, __func__, ##__VA_ARGS__)
#else
#define ipmi_debug(fs, ...)
#endif

struct ipmi_sdr_header {
    uint8_t  rec_id[2];
    uint8_t  sdr_version;               /* 0x51 */
    uint8_t  rec_type;
    uint8_t  rec_length;
};
#define IPMI_SDR_HEADER_SIZE     sizeof(struct ipmi_sdr_header)

#define ipmi_sdr_recid(sdr) ((sdr)->rec_id[0] | ((sdr)->rec_id[1] << 8))
#define ipmi_sdr_length(sdr) ((sdr)->rec_length + IPMI_SDR_HEADER_SIZE)

/*
 * 43.2 SDR Type 02h. Compact Sensor Record
 */
#define IPMI_SDR_COMPACT_TYPE    2

struct ipmi_sdr_compact {
    struct ipmi_sdr_header header;

    uint8_t  sensor_owner_id;
    uint8_t  sensor_owner_lun;
    uint8_t  sensor_owner_number;       /* byte 8 */
    uint8_t  entity_id;
    uint8_t  entity_instance;
    uint8_t  sensor_init;
    uint8_t  sensor_caps;
    uint8_t  sensor_type;
    uint8_t  reading_type;
    uint8_t  assert_mask[2];            /* byte 16 */
    uint8_t  deassert_mask[2];
    uint8_t  discrete_mask[2];
    uint8_t  sensor_unit1;
    uint8_t  sensor_unit2;
    uint8_t  sensor_unit3;
    uint8_t  sensor_direction[2];       /* byte 24 */
    uint8_t  positive_threshold;
    uint8_t  negative_threshold;
    uint8_t  reserved[3];
    uint8_t  oem;
    uint8_t  id_str_len;                /* byte 32 */
    uint8_t  id_string[16];
};

typedef uint8_t ipmi_sdr_compact_buffer[sizeof(struct ipmi_sdr_compact)];

int ipmi_bmc_sdr_find(IPMIBmc *b, uint16_t recid,
                      const struct ipmi_sdr_compact **sdr, uint16_t *nextrec);
void ipmi_bmc_gen_event(IPMIBmc *b, uint8_t *evt, bool log);

#endif
