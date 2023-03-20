#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "hw/acpi/tpm.h"
#include "migration/vmstate.h"
#include "tpm_prop.h"
#include "tpm_tis.h"
#include "qom/object.h"
#include "block/aio.h"
#include "qemu/main-loop.h"

typedef struct TPMStateI2C {
    /*< private >*/
    I2CSlave parent_obj;

    /* TODO: Work on getting rid of this buffer */
    int      offset;
    int      size;
    uint8_t  operation;  /* 1 - send and 2 - recv */
    uint8_t  data[4096];

    /*< public >*/
    TPMState state; /* not a QOM object */

} TPMStateI2C;

DECLARE_INSTANCE_CHECKER(TPMStateI2C, TPM_TIS_I2C,
                         TYPE_TPM_TIS_I2C)

static const VMStateDescription vmstate_tpm_tis_i2c = {
    .name = "tpm",
    .unmigratable = 1,
};

/* Register map */
typedef struct reg_map {
    uint16_t  i2c_reg;    /* I2C register */
    uint16_t  tis_reg;    /* TIS register */
    uint32_t  data_size;  /* data size expected */
} i2c_reg_map;

#define TPM_I2C_MAP_COUNT 11

/* The register values in the common code is different than the latest
 * register numbers as per the spec hence add the conversion map
 */
i2c_reg_map tpm_tis_reg_map[] =
{
    { 0x00, TPM_TIS_REG_ACCESS,           1, },
    { 0x04, TPM_TIS_REG_ACCESS,           1, },
    { 0x08, TPM_TIS_REG_INT_ENABLE,       4, },
    { 0x14, TPM_TIS_REG_INT_VECTOR,       4, },
    { 0x18, TPM_TIS_REG_STS,              4, },
    { 0x24, TPM_TIS_REG_DATA_FIFO,        0, },
    { 0x30, TPM_TIS_REG_INTF_CAPABILITY,  4, },
    { 0x40, TPM_TIS_REG_DATA_CSUM_ENABLE, 1, },
    { 0x44, TPM_TIS_REG_DATA_CSUM_GET,    2, },
    { 0x48, TPM_TIS_REG_DID_VID,          4, },
    { 0x4C, TPM_TIS_REG_RID,              1, },
};

static inline uint16_t tpm_tis_i2c_to_tis_reg(uint64_t i2c_reg, int *size)
{
    uint16_t tis_reg = 0xff;
    i2c_reg_map *reg_map;
    int i;

    for (i = 0; i < TPM_I2C_MAP_COUNT; i++)
    {
	reg_map = &tpm_tis_reg_map[i];
	if (reg_map->i2c_reg == i2c_reg)
	{
            tis_reg = reg_map->tis_reg;
	    *size = reg_map->data_size;
	    break;
	}
    }

    assert(tis_reg != 0xff);
    return tis_reg;
}

static inline void tpm_tis_i2c_tpm_start_send(TPMStateI2C *i2cst)
{
    /* Clear operation and offset */ 
    i2cst->operation = 0;
    i2cst->offset = 0;
    i2cst->size = 0;

    return;
}

/* Send data to TPM */
static inline void tpm_tis_i2c_tpm_send(TPMStateI2C *i2cst)
{

    if ((i2cst->operation == 1) && (i2cst->offset >= 2))
    {
        uint16_t tis_reg;
        uint32_t data;
        int      i;

        tis_reg = tpm_tis_i2c_to_tis_reg(i2cst->data[0], &i2cst->size);

	for (i = 1; i < i2cst->offset; i++)
	{
            data = (i2cst->data[i] & 0xff);
            tpm_tis_write_data(&i2cst->state, tis_reg, data, 1);
        }

	i2cst->operation = 0;
	i2cst->offset = 0;
	i2cst->size = 0;
    }

    return;
}

/* Callback from TPM */
static void tpm_tis_i2c_request_completed(TPMIf *ti, int ret)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(ti);
    TPMState *s = &i2cst->state;

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

#ifdef TRACE_TPM_I2C
    tpm_tis_i2c_event_string(event, event_name);
#endif

    switch (event) {
    case I2C_START_RECV:
        break;
    case I2C_START_SEND:
        tpm_tis_i2c_tpm_start_send(i2cst);
        break;
    case I2C_FINISH:
	if (i2cst->operation == 1)
            tpm_tis_i2c_tpm_send(i2cst);
	else
	{
            i2cst->operation = 0;
            i2cst->offset = 0;
            i2cst->size = 0;
	}
        break;
    default:
        break;
    }

    return ret;
}

static uint8_t tpm_tis_i2c_recv(I2CSlave *i2c)
{
    int ret = 0;
    int i, j;
    uint32_t addr;
    uint32_t data_read;
    uint16_t i2c_reg;
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);
    TPMState *s = &i2cst->state;

    if (i2cst->operation == 2)
    {
	/* Special handling for FIFO */
        if (i2cst->data[0] == 0x24)
        {
	    i2c_reg = i2cst->data[0];
            addr = tpm_tis_i2c_to_tis_reg (i2c_reg, &i2cst->size);
            data_read = tpm_tis_read_data(s, addr, 1);
	    ret = (data_read & 0xff);
	}
	else
            ret = i2cst->data[i2cst->offset++];
    }
    else if ((i2cst->operation == 1) && (i2cst->offset < 2))
    {
	i2c_reg = i2cst->data[0];

	i2cst->operation = 2;
	i2cst->offset = 0;

        addr = tpm_tis_i2c_to_tis_reg (i2c_reg, &i2cst->size);

	/* Special handling for FIFO register */
        if (i2c_reg == 0x24)
        {
            data_read = tpm_tis_read_data(s, addr, 1);
 	    ret = (data_read & 0xff);
        }
        else
        {
            /* Save the data in the data field. Save it in the little
             * endian format.
             */
            for (i = 0; i < i2cst->size;)
            {
                data_read = tpm_tis_read_data(s, addr, 4);
                for (j = 0; j < 4; j++)
	        {
                    i2cst->data[i++] = (data_read & 0xff); 
	            data_read >>= 8;
                }
	    }

	    /* Return first byte with this call */
	    ret = i2cst->data[i2cst->offset++];
        }
    }
    else
        i2cst->operation = 2;

    return ret;
}

static int tpm_tis_i2c_send(I2CSlave *i2c, uint8_t data)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(i2c);

    /* Remember data locally */
    i2cst->operation = 1;
    i2cst->data[i2cst->offset++] = data;

    return 0;
}

static Property tpm_tis_i2c_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMStateI2C, state.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_TPMBE("tpmdev", TPMStateI2C, state.be_driver),
    DEFINE_PROP_BOOL("ppi", TPMStateI2C, state.ppi_enabled, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_i2c_realizefn(DeviceState *dev, Error **errp)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    /* TODO: Backend was not initialized hence had to put workaround */
    s->be_driver = qemu_find_tpm_be("tpm0");

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
    if (s->irq_num > 15) {
        error_setg(errp, "IRQ %d is outside valid range of 0 to 15",
                   s->irq_num);
        return;
    }
}

static void tpm_tis_i2c_reset(DeviceState *dev)
{
    TPMStateI2C *i2cst = TPM_TIS_I2C(dev);
    TPMState *s = &i2cst->state;

    i2cst->offset = 0;
    i2cst->operation = 0;

    return tpm_tis_reset(s);
}

static void tpm_tis_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    dc->realize = tpm_tis_i2c_realizefn;
    dc->reset = tpm_tis_i2c_reset;
    dc->vmsd = &vmstate_tpm_tis_i2c;
    device_class_set_props(dc, tpm_tis_i2c_properties);

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
        .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_i2c_register_types(void)
{
    type_register_static(&tpm_tis_i2c_info);
}

type_init(tpm_tis_i2c_register_types)
