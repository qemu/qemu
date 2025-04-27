/*
 * tpm_tis_i2c.c - QEMU's TPM TIS I2C Device
 *
 * Copyright (c) 2023 IBM Corporation
 *
 * Authors:
 *   Ninad Palsule <ninad@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * TPM I2C implementation follows TCG TPM I2c Interface specification,
 * Family 2.0, Level 00, Revision 1.00
 *
 * TPM TIS for TPM 2 implementation following TCG PC Client Platform
 * TPM Profile (PTP) Specification, Family 2.0, Revision 00.43
 *
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/sysbus.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "tpm_prop.h"
#include "qemu/log.h"
#include "trace.h"
#include "tpm_tis.h"

/* Operations */
#define OP_SEND   1
#define OP_RECV   2

/* Is locality valid */
#define TPM_TIS_I2C_IS_VALID_LOCTY(x)   TPM_TIS_IS_VALID_LOCTY(x)

typedef struct TPMStateI2C {
    /*< private >*/
    I2CSlave    parent_obj;

    uint8_t     offset;       /* offset into data[] */
    uint8_t     operation;    /* OP_SEND & OP_RECV */
    uint8_t     data[5];      /* Data */

    /* i2c registers */
    uint8_t     loc_sel;      /* Current locality */
    uint8_t     csum_enable;  /* Is checksum enabled */

    /* Derived from the above */
    const char *reg_name;     /* Register name */
    uint32_t    tis_addr;     /* Converted tis address including locty */

    /*< public >*/
    TPMState    state; /* not a QOM object */

} TPMStateI2C;

DECLARE_INSTANCE_CHECKER(TPMStateI2C, TPM_TIS_I2C,
                         TYPE_TPM_TIS_I2C)

/* Prototype */
static inline void tpm_tis_i2c_to_tis_reg(TPMStateI2C *i2cst, uint8_t i2c_reg);

/* Register map */
typedef struct regMap {
    uint8_t   i2c_reg;    /* I2C register */
    uint16_t  tis_reg;    /* TIS register */
    const char *reg_name; /* Register name */
} I2CRegMap;

/*
 * The register values in the common code is different than the latest
 * register numbers as per the spec hence add the conversion map
 */
static const I2CRegMap tpm_tis_reg_map[] = {
    /*
     * These registers are sent to TIS layer. The register with UNKNOWN
     * mapping are not sent to TIS layer and handled in I2c layer.
     * NOTE: Adding frequently used registers at the start
     */
    { TPM_I2C_REG_DATA_FIFO,        TPM_TIS_REG_DATA_FIFO,       "FIFO",      },
    { TPM_I2C_REG_STS,              TPM_TIS_REG_STS,             "STS",       },
    { TPM_I2C_REG_DATA_CSUM_GET,    TPM_I2C_REG_UNKNOWN,         "CSUM_GET",  },
    { TPM_I2C_REG_LOC_SEL,          TPM_I2C_REG_UNKNOWN,         "LOC_SEL",   },
    { TPM_I2C_REG_ACCESS,           TPM_TIS_REG_ACCESS,          "ACCESS",    },
    { TPM_I2C_REG_INT_ENABLE,       TPM_TIS_REG_INT_ENABLE,     "INTR_ENABLE",},
    { TPM_I2C_REG_INT_CAPABILITY,   TPM_I2C_REG_UNKNOWN,         "INTR_CAP",  },
    { TPM_I2C_REG_INTF_CAPABILITY,  TPM_TIS_REG_INTF_CAPABILITY, "INTF_CAP",  },
    { TPM_I2C_REG_DID_VID,          TPM_TIS_REG_DID_VID,         "DID_VID",   },
    { TPM_I2C_REG_RID,              TPM_TIS_REG_RID,             "RID",       },
    { TPM_I2C_REG_I2C_DEV_ADDRESS,  TPM_I2C_REG_UNKNOWN,        "DEV_ADDRESS",},
    { TPM_I2C_REG_DATA_CSUM_ENABLE, TPM_I2C_REG_UNKNOWN,        "CSUM_ENABLE",},
};

static int tpm_tis_i2c_pre_save(void *opaque)
{
    TPMStateI2C *i2cst = opaque;

    return tpm_tis_pre_save(&i2cst->state);
}

static int tpm_tis_i2c_post_load(void *opaque, int version_id)
{
    TPMStateI2C *i2cst = opaque;

    if (i2cst->offset >= 1) {
        tpm_tis_i2c_to_tis_reg(i2cst, i2cst->data[0]);
    }

    return 0;
}

static const VMStateDescription vmstate_tpm_tis_i2c = {
    .name = "tpm-tis-i2c",
    .version_id = 0,
    .pre_save  = tpm_tis_i2c_pre_save,
    .post_load  = tpm_tis_i2c_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_BUFFER(state.buffer, TPMStateI2C),
        VMSTATE_UINT16(state.rw_offset, TPMStateI2C),
        VMSTATE_UINT8(state.active_locty, TPMStateI2C),
        VMSTATE_UINT8(state.aborting_locty, TPMStateI2C),
        VMSTATE_UINT8(state.next_locty, TPMStateI2C),

        VMSTATE_STRUCT_ARRAY(state.loc, TPMStateI2C, TPM_TIS_NUM_LOCALITIES, 0,
                             vmstate_locty, TPMLocality),

        /* i2c specifics */
        VMSTATE_UINT8(offset, TPMStateI2C),
        VMSTATE_UINT8(operation, TPMStateI2C),
        VMSTATE_BUFFER(data, TPMStateI2C),
        VMSTATE_UINT8(loc_sel, TPMStateI2C),
        VMSTATE_UINT8(csum_enable, TPMStateI2C),

        VMSTATE_END_OF_LIST()
    }
};

/*
 * Set data value. The i2cst->offset is not updated as called in
 * the read path.
 */
static void tpm_tis_i2c_set_data(TPMStateI2C *i2cst, uint32_t data)
{
    i2cst->data[1] = data;
    i2cst->data[2] = data >> 8;
    i2cst->data[3] = data >> 16;
    i2cst->data[4] = data >> 24;
}
/*
 * Generate interface capability based on what is returned by TIS and what is
 * expected by I2C. Save the capability in the data array overwriting the TIS
 * capability.
 */
static uint32_t tpm_tis_i2c_interface_capability(TPMStateI2C *i2cst,
                                                 uint32_t tis_cap)
{
    uint32_t i2c_cap;

    /* Now generate i2c capability */
    i2c_cap = (TPM_I2C_CAP_INTERFACE_TYPE |
               TPM_I2C_CAP_INTERFACE_VER  |
               TPM_I2C_CAP_TPM2_FAMILY    |
               TPM_I2C_CAP_LOCALITY_CAP   |
               TPM_I2C_CAP_BUS_SPEED      |
               TPM_I2C_CAP_DEV_ADDR_CHANGE);

    /* Now check the TIS and set some capabilities */

    /* Static burst count set */
    if (tis_cap & TPM_TIS_CAP_BURST_COUNT_STATIC) {
        i2c_cap |= TPM_I2C_CAP_BURST_COUNT_STATIC;
    }

    return i2c_cap;
}

/* Convert I2C register to TIS address and returns the name of the register */
static inline void tpm_tis_i2c_to_tis_reg(TPMStateI2C *i2cst, uint8_t i2c_reg)
{
    const I2CRegMap *reg_map;
    int i;

    i2cst->tis_addr = 0xffffffff;

    /* Special case for the STS register. */
    if (i2c_reg >= TPM_I2C_REG_STS && i2c_reg <= TPM_I2C_REG_STS + 3) {
        i2c_reg = TPM_I2C_REG_STS;
    }

    for (i = 0; i < ARRAY_SIZE(tpm_tis_reg_map); i++) {
        reg_map = &tpm_tis_reg_map[i];
        if (reg_map->i2c_reg == i2c_reg) {
            i2cst->reg_name = reg_map->reg_name;
            i2cst->tis_addr = reg_map->tis_reg;

            /* Include the locality in the address. */
            assert(TPM_TIS_I2C_IS_VALID_LOCTY(i2cst->loc_sel));
            i2cst->tis_addr += (i2cst->loc_sel << TPM_TIS_LOCALITY_SHIFT);
            break;
        }
    }
}

/* Clear some fields from the structure. */
static inline void tpm_tis_i2c_clear_data(TPMStateI2C *i2cst)
{
    /* Clear operation and offset */
    i2cst->operation = 0;
    i2cst->offset = 0;
    i2cst->tis_addr = 0xffffffff;
    i2cst->reg_name = NULL;
    memset(i2cst->data, 0, sizeof(i2cst->data));
}

/* Send data to TPM */
static inline void tpm_tis_i2c_tpm_send(TPMStateI2C *i2cst)
{
    uint32_t data;
    size_t offset = 0;
    uint32_t sz = 4;

    if ((i2cst->operation == OP_SEND) && (i2cst->offset > 1)) {

        switch (i2cst->data[0]) {
        case TPM_I2C_REG_DATA_CSUM_ENABLE:
            /*
             * Checksum is not handled by TIS code hence we will consume the
             * register here.
             */
            i2cst->csum_enable = i2cst->data[1] & TPM_DATA_CSUM_ENABLED;
            break;
        case TPM_I2C_REG_DATA_FIFO:
            /* Handled in the main i2c_send function */
            break;
        case TPM_I2C_REG_LOC_SEL:
            /*
             * This register is not handled by TIS so save the locality
             * locally
             */
            if (TPM_TIS_I2C_IS_VALID_LOCTY(i2cst->data[1])) {
                i2cst->loc_sel = i2cst->data[1];
            }
            break;
        default:
            /* We handle non-FIFO here */

            /* Index 0 is a register. Convert byte stream to uint32_t */
            data = i2cst->data[1];
            data |= i2cst->data[2] << 8;
            data |= i2cst->data[3] << 16;
            data |= i2cst->data[4] << 24;

            /* Add register specific masking */
            switch (i2cst->data[0]) {
            case TPM_I2C_REG_INT_ENABLE:
                data &= TPM_I2C_INT_ENABLE_MASK;
                break;
            case TPM_I2C_REG_STS ... TPM_I2C_REG_STS + 3:
                /*
                 * STS register has 4 bytes data.
                 * As per the specs following writes must be allowed.
                 *  - From base address 1 to 4 bytes are allowed.
                 *  - Single byte write to first or last byte must
                 *    be allowed.
                 */
                offset = i2cst->data[0] - TPM_I2C_REG_STS;
                if (offset > 0) {
                    sz = 1;
                }
                data &= (TPM_I2C_STS_WRITE_MASK >> (offset * 8));
                break;
            }

            tpm_tis_write_data(&i2cst->state, i2cst->tis_addr + offset, data,
                               sz);
            break;
        }

        tpm_tis_i2c_clear_data(i2cst);
    }
}

/* Callback from TPM to indicate that response is copied */
static void tpm_tis_i2c_request_completed(TPMIf *ti, int ret)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

    /* Inform the common code. */
    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_i2c_get_tpm_version(TPMIf *ti)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

    return tpm_tis_get_tpm_version(s);
}

static int tpm_tis_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    int ret = 0;

    switch (event) {
    case I2C_START_RECV:
        trace_tpm_tis_i2c_event("START_RECV");
        break;
    case I2C_START_SEND:
        trace_tpm_tis_i2c_event("START_SEND");
        tpm_tis_i2c_clear_data(i2cst);
        break;
    case I2C_FINISH:
        trace_tpm_tis_i2c_event("FINISH");
        if (i2cst->operation == OP_SEND) {
            tpm_tis_i2c_tpm_send(i2cst);
        } else {
            tpm_tis_i2c_clear_data(i2cst);
        }
        break;
    default:
        break;
    }

    return ret;
}

/*
 * If data is for FIFO then it is received from tpm_tis_common buffer
 * otherwise it will be handled using single call to common code and
 * cached in the local buffer.
 */
static uint8_t tpm_tis_i2c_recv(I2CSlave *i2c)
{
    int          ret = 0;
    uint32_t     data_read;
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    TPMState    *s = &i2cst->state;
    uint16_t     i2c_reg = i2cst->data[0];
    size_t       offset;

    if (i2cst->operation == OP_RECV) {

        /* Do not cache FIFO data. */
        if (i2cst->data[0] == TPM_I2C_REG_DATA_FIFO) {
            data_read = tpm_tis_read_data(s, i2cst->tis_addr, 1);
            ret = (data_read & 0xff);
        } else if (i2cst->offset < sizeof(i2cst->data)) {
            ret = i2cst->data[i2cst->offset++];
        }

    } else if ((i2cst->operation == OP_SEND) && (i2cst->offset < 2)) {
        /* First receive call after send */

        i2cst->operation = OP_RECV;

        switch (i2c_reg) {
        case TPM_I2C_REG_LOC_SEL:
            /* Location selection register is managed by i2c */
            tpm_tis_i2c_set_data(i2cst, i2cst->loc_sel);
            break;
        case TPM_I2C_REG_DATA_FIFO:
            /* FIFO data is directly read from TPM TIS */
            data_read = tpm_tis_read_data(s, i2cst->tis_addr, 1);
            tpm_tis_i2c_set_data(i2cst, (data_read & 0xff));
            break;
        case TPM_I2C_REG_DATA_CSUM_ENABLE:
            tpm_tis_i2c_set_data(i2cst, i2cst->csum_enable);
            break;
        case TPM_I2C_REG_INT_CAPABILITY:
            /*
             * Interrupt is not supported in the linux kernel hence we cannot
             * test this model with interrupts.
             */
            tpm_tis_i2c_set_data(i2cst, TPM_I2C_INT_ENABLE_MASK);
            break;
        case TPM_I2C_REG_DATA_CSUM_GET:
            /*
             * Checksum registers are not supported by common code hence
             * call a common code to get the checksum.
             */
            data_read = tpm_tis_get_checksum(s);

            /* Save the byte stream in data field */
            tpm_tis_i2c_set_data(i2cst, data_read);
            break;
        default:
            data_read = tpm_tis_read_data(s, i2cst->tis_addr, 4);

            switch (i2c_reg) {
            case TPM_I2C_REG_INTF_CAPABILITY:
                /* Prepare the capabilities as per I2C interface */
                data_read = tpm_tis_i2c_interface_capability(i2cst,
                                                             data_read);
                break;
            case TPM_I2C_REG_STS ... TPM_I2C_REG_STS + 3:
                offset = i2c_reg - TPM_I2C_REG_STS;
                /*
                 * As per specs, STS bit 31:26 are reserved and must
                 * be set to 0
                 */
                data_read &= TPM_I2C_STS_READ_MASK;
                /*
                 * STS register has 4 bytes data.
                 * As per the specs following reads must be allowed.
                 *  - From base address 1 to 4 bytes are allowed.
                 *  - Last byte must be allowed to read as a single byte
                 *  - Second and third byte must be allowed to read as two
                 *    two bytes.
                 */
                data_read >>= (offset * 8);
                break;
            }

            /* Save byte stream in data[] */
            tpm_tis_i2c_set_data(i2cst, data_read);
            break;
        }

        /* Return first byte with this call */
        i2cst->offset = 1; /* keep the register value intact for debug */
        ret = i2cst->data[i2cst->offset++];
    } else {
        i2cst->operation = OP_RECV;
    }

    trace_tpm_tis_i2c_recv(ret);

    return ret;
}

/*
 * Send function only remembers data in the buffer and then calls
 * TPM TIS common code during FINISH event.
 */
static int tpm_tis_i2c_send(I2CSlave *i2c, uint8_t data)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);

    /* Reject non-supported registers. */
    if (i2cst->offset == 0) {
        /* Convert I2C register to TIS register */
        tpm_tis_i2c_to_tis_reg(i2cst, data);
        if (i2cst->tis_addr == 0xffffffff) {
            return 0xffffffff;
        }

        trace_tpm_tis_i2c_send_reg(i2cst->reg_name, data);

        /* We do not support device address change */
        if (data == TPM_I2C_REG_I2C_DEV_ADDRESS) {
            qemu_log_mask(LOG_UNIMP, "%s: Device address change "
                          "is not supported.\n", __func__);
            return 0xffffffff;
        }
    } else {
        trace_tpm_tis_i2c_send(data);
    }

    if (i2cst->offset < sizeof(i2cst->data)) {
        i2cst->operation = OP_SEND;

        /*
         * In two cases, we save values in the local buffer.
         * 1) The first value is always a register.
         * 2) In case of non-FIFO multibyte registers, TIS expects full
         *    register value hence I2C layer cache the register value and send
         *    to TIS during FINISH event.
         */
        if ((i2cst->offset == 0) ||
            (i2cst->data[0] != TPM_I2C_REG_DATA_FIFO)) {
            i2cst->data[i2cst->offset++] = data;
        } else {
            /*
             * The TIS can process FIFO data one byte at a time hence the FIFO
             * data is sent to TIS directly.
             */
            tpm_tis_write_data(&i2cst->state, i2cst->tis_addr, data, 1);
        }

        return 0;
    }

    /* Return non-zero to indicate NAK */
    return 1;
}

static const Property tpm_tis_i2c_properties[] = {
    DEFINE_PROP_TPMBE("tpmdev", TPMStateI2C, state.be_driver),
};

static void tpm_tis_i2c_realizefn(DeviceState *dev, Error **errp)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    /*
     * Get the backend pointer. It is not initialized properly during
     * device_class_set_props
     */
    s->be_driver = qemu_find_tpm_be("tpm0");

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
}

static void tpm_tis_i2c_reset(DeviceState *dev)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    tpm_tis_i2c_clear_data(i2cst);

    i2cst->csum_enable = 0;
    i2cst->loc_sel = 0x00;

    return tpm_tis_reset(s);
}

static void tpm_tis_i2c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_tis_i2c_realizefn;
    device_class_set_legacy_reset(dc, tpm_tis_i2c_reset);
    dc->vmsd = &vmstate_tpm_tis_i2c;
    device_class_set_props(dc, tpm_tis_i2c_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    k->event = tpm_tis_i2c_event;
    k->recv = tpm_tis_i2c_recv;
    k->send = tpm_tis_i2c_send;

    tc->model = TPM_MODEL_TPM_TIS;
    tc->request_completed = tpm_tis_i2c_request_completed;
    tc->get_version = tpm_tis_i2c_get_tpm_version;
}

static const TypeInfo tpm_tis_i2c_info = {
    .name          = TYPE_TPM_TIS_I2C,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TPMStateI2C),
    .class_init    = tpm_tis_i2c_class_init,
        .interfaces = (const InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_i2c_register_types(void)
{
    type_register_static(&tpm_tis_i2c_info);
}

type_init(tpm_tis_i2c_register_types)
