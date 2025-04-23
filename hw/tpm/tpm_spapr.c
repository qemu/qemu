/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * PAPR Virtual TPM
 *
 * Copyright (c) 2015, 2017, 2019 IBM Corporation.
 *
 * Authors:
 *    Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#include "system/tpm_backend.h"
#include "system/tpm_util.h"
#include "tpm_prop.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "trace.h"
#include "qom/object.h"

#define DEBUG_SPAPR 0

typedef struct SpaprTpmState SpaprTpmState;
DECLARE_INSTANCE_CHECKER(SpaprTpmState, VIO_SPAPR_VTPM,
                         TYPE_TPM_SPAPR)

typedef struct TpmCrq {
    uint8_t valid;  /* 0x80: cmd; 0xc0: init crq */
                    /* 0x81-0x83: CRQ message response */
    uint8_t msg;    /* see below */
    uint16_t len;   /* len of TPM request; len of TPM response */
    uint32_t data;  /* rtce_dma_handle when sending TPM request */
    uint64_t reserved;
} TpmCrq;

#define SPAPR_VTPM_VALID_INIT_CRQ_COMMAND  0xC0
#define SPAPR_VTPM_VALID_COMMAND           0x80
#define SPAPR_VTPM_MSG_RESULT              0x80

/* msg types for valid = SPAPR_VTPM_VALID_INIT_CRQ */
#define SPAPR_VTPM_INIT_CRQ_RESULT           0x1
#define SPAPR_VTPM_INIT_CRQ_COMPLETE_RESULT  0x2

/* msg types for valid = SPAPR_VTPM_VALID_CMD */
#define SPAPR_VTPM_GET_VERSION               0x1
#define SPAPR_VTPM_TPM_COMMAND               0x2
#define SPAPR_VTPM_GET_RTCE_BUFFER_SIZE      0x3
#define SPAPR_VTPM_PREPARE_TO_SUSPEND        0x4

/* response error messages */
#define SPAPR_VTPM_VTPM_ERROR                0xff

/* error codes */
#define SPAPR_VTPM_ERR_COPY_IN_FAILED        0x3
#define SPAPR_VTPM_ERR_COPY_OUT_FAILED       0x4

#define TPM_SPAPR_BUFFER_MAX                 4096

struct SpaprTpmState {
    SpaprVioDevice vdev;

    TpmCrq crq; /* track single TPM command */

    uint8_t state;
#define SPAPR_VTPM_STATE_NONE         0
#define SPAPR_VTPM_STATE_EXECUTION    1
#define SPAPR_VTPM_STATE_COMPLETION   2

    unsigned char *buffer;

    uint32_t numbytes; /* number of bytes to deliver on resume */

    TPMBackendCmd cmd;

    TPMBackend *be_driver;
    TPMVersion be_tpm_version;

    size_t be_buffer_size;
};

/*
 * Send a request to the TPM.
 */
static void tpm_spapr_tpm_send(SpaprTpmState *s)
{
    tpm_util_show_buffer(s->buffer, s->be_buffer_size, "To TPM");

    s->state = SPAPR_VTPM_STATE_EXECUTION;
    s->cmd = (TPMBackendCmd) {
        .locty = 0,
        .in = s->buffer,
        .in_len = MIN(tpm_cmd_get_size(s->buffer), s->be_buffer_size),
        .out = s->buffer,
        .out_len = s->be_buffer_size,
    };

    tpm_backend_deliver_request(s->be_driver, &s->cmd);
}

static int tpm_spapr_process_cmd(SpaprTpmState *s, uint64_t dataptr)
{
    long rc;

    /* a max. of be_buffer_size bytes can be transported */
    rc = spapr_vio_dma_read(&s->vdev, dataptr,
                            s->buffer, s->be_buffer_size);
    if (rc) {
        error_report("tpm_spapr_got_payload: DMA read failure");
    }
    /* let vTPM handle any malformed request */
    tpm_spapr_tpm_send(s);

    return rc;
}

static inline int spapr_tpm_send_crq(struct SpaprVioDevice *dev, TpmCrq *crq)
{
    return spapr_vio_send_crq(dev, (uint8_t *)crq);
}

static int tpm_spapr_do_crq(struct SpaprVioDevice *dev, uint8_t *crq_data)
{
    SpaprTpmState *s = VIO_SPAPR_VTPM(dev);
    TpmCrq local_crq;
    TpmCrq *crq = &s->crq; /* requests only */
    int rc;
    uint8_t valid = crq_data[0];
    uint8_t msg = crq_data[1];

    trace_tpm_spapr_do_crq(valid, msg);

    switch (valid) {
    case SPAPR_VTPM_VALID_INIT_CRQ_COMMAND: /* Init command/response */

        /* Respond to initialization request */
        switch (msg) {
        case SPAPR_VTPM_INIT_CRQ_RESULT:
            trace_tpm_spapr_do_crq_crq_result();
            memset(&local_crq, 0, sizeof(local_crq));
            local_crq.valid = SPAPR_VTPM_VALID_INIT_CRQ_COMMAND;
            local_crq.msg = SPAPR_VTPM_INIT_CRQ_RESULT;
            spapr_tpm_send_crq(dev, &local_crq);
            break;

        case SPAPR_VTPM_INIT_CRQ_COMPLETE_RESULT:
            trace_tpm_spapr_do_crq_crq_complete_result();
            memset(&local_crq, 0, sizeof(local_crq));
            local_crq.valid = SPAPR_VTPM_VALID_INIT_CRQ_COMMAND;
            local_crq.msg = SPAPR_VTPM_INIT_CRQ_COMPLETE_RESULT;
            spapr_tpm_send_crq(dev, &local_crq);
            break;
        }

        break;
    case SPAPR_VTPM_VALID_COMMAND: /* Payloads */
        switch (msg) {
        case SPAPR_VTPM_TPM_COMMAND:
            trace_tpm_spapr_do_crq_tpm_command();
            if (s->state == SPAPR_VTPM_STATE_EXECUTION) {
                return H_BUSY;
            }
            memcpy(crq, crq_data, sizeof(*crq));

            rc = tpm_spapr_process_cmd(s, be32_to_cpu(crq->data));

            if (rc == H_SUCCESS) {
                crq->valid = be16_to_cpu(0);
            } else {
                local_crq.valid = SPAPR_VTPM_MSG_RESULT;
                local_crq.msg = SPAPR_VTPM_VTPM_ERROR;
                local_crq.len = cpu_to_be16(0);
                local_crq.data = cpu_to_be32(SPAPR_VTPM_ERR_COPY_IN_FAILED);
                spapr_tpm_send_crq(dev, &local_crq);
            }
            break;

        case SPAPR_VTPM_GET_RTCE_BUFFER_SIZE:
            trace_tpm_spapr_do_crq_tpm_get_rtce_buffer_size(s->be_buffer_size);
            local_crq.valid = SPAPR_VTPM_VALID_COMMAND;
            local_crq.msg = SPAPR_VTPM_GET_RTCE_BUFFER_SIZE |
                            SPAPR_VTPM_MSG_RESULT;
            local_crq.len = cpu_to_be16(s->be_buffer_size);
            spapr_tpm_send_crq(dev, &local_crq);
            break;

        case SPAPR_VTPM_GET_VERSION:
            local_crq.valid = SPAPR_VTPM_VALID_COMMAND;
            local_crq.msg = SPAPR_VTPM_GET_VERSION | SPAPR_VTPM_MSG_RESULT;
            local_crq.len = cpu_to_be16(0);
            switch (s->be_tpm_version) {
            case TPM_VERSION_1_2:
                local_crq.data = cpu_to_be32(1);
                break;
            case TPM_VERSION_2_0:
                local_crq.data = cpu_to_be32(2);
                break;
            default:
                g_assert_not_reached();
            }
            trace_tpm_spapr_do_crq_get_version(be32_to_cpu(local_crq.data));
            spapr_tpm_send_crq(dev, &local_crq);
            break;

        case SPAPR_VTPM_PREPARE_TO_SUSPEND:
            trace_tpm_spapr_do_crq_prepare_to_suspend();
            local_crq.valid = SPAPR_VTPM_VALID_COMMAND;
            local_crq.msg = SPAPR_VTPM_PREPARE_TO_SUSPEND |
                            SPAPR_VTPM_MSG_RESULT;
            spapr_tpm_send_crq(dev, &local_crq);
            break;

        default:
            trace_tpm_spapr_do_crq_unknown_msg_type(crq->msg);
        }
        break;
    default:
        trace_tpm_spapr_do_crq_unknown_crq(valid, msg);
    };

    return H_SUCCESS;
}

static void tpm_spapr_request_completed(TPMIf *ti, int ret)
{
    SpaprTpmState *s = VIO_SPAPR_VTPM(ti);
    TpmCrq *crq = &s->crq;
    uint32_t len;
    int rc;

    s->state = SPAPR_VTPM_STATE_COMPLETION;

    /* a max. of be_buffer_size bytes can be transported */
    len = MIN(tpm_cmd_get_size(s->buffer), s->be_buffer_size);

    if (runstate_check(RUN_STATE_FINISH_MIGRATE)) {
        trace_tpm_spapr_caught_response(len);
        /* defer delivery of response until .post_load */
        s->numbytes = len;
        return;
    }

    rc = spapr_vio_dma_write(&s->vdev, be32_to_cpu(crq->data),
                             s->buffer, len);

    tpm_util_show_buffer(s->buffer, len, "From TPM");

    crq->valid = SPAPR_VTPM_MSG_RESULT;
    if (rc == H_SUCCESS) {
        crq->msg = SPAPR_VTPM_TPM_COMMAND | SPAPR_VTPM_MSG_RESULT;
        crq->len = cpu_to_be16(len);
    } else {
        error_report("%s: DMA write failure", __func__);
        crq->msg = SPAPR_VTPM_VTPM_ERROR;
        crq->len = cpu_to_be16(0);
        crq->data = cpu_to_be32(SPAPR_VTPM_ERR_COPY_OUT_FAILED);
    }

    rc = spapr_tpm_send_crq(&s->vdev, crq);
    if (rc) {
        error_report("%s: Error sending response", __func__);
    }
}

static int tpm_spapr_do_startup_tpm(SpaprTpmState *s, size_t buffersize)
{
    return tpm_backend_startup_tpm(s->be_driver, buffersize);
}

static const char *tpm_spapr_get_dt_compatible(SpaprVioDevice *dev)
{
    SpaprTpmState *s = VIO_SPAPR_VTPM(dev);

    switch (s->be_tpm_version) {
    case TPM_VERSION_1_2:
        return "IBM,vtpm";
    case TPM_VERSION_2_0:
        return "IBM,vtpm20";
    default:
        g_assert_not_reached();
    }
}

static void tpm_spapr_reset(SpaprVioDevice *dev)
{
    SpaprTpmState *s = VIO_SPAPR_VTPM(dev);

    s->state = SPAPR_VTPM_STATE_NONE;
    s->numbytes = 0;

    s->be_tpm_version = tpm_backend_get_tpm_version(s->be_driver);

    s->be_buffer_size = MIN(tpm_backend_get_buffer_size(s->be_driver),
                            TPM_SPAPR_BUFFER_MAX);

    tpm_backend_reset(s->be_driver);

    if (tpm_spapr_do_startup_tpm(s, s->be_buffer_size) < 0) {
        exit(1);
    }
}

static enum TPMVersion tpm_spapr_get_version(TPMIf *ti)
{
    SpaprTpmState *s = VIO_SPAPR_VTPM(ti);

    if (tpm_backend_had_startup_error(s->be_driver)) {
        return TPM_VERSION_UNSPEC;
    }

    return tpm_backend_get_tpm_version(s->be_driver);
}

/* persistent state handling */

static int tpm_spapr_pre_save(void *opaque)
{
    SpaprTpmState *s = opaque;

    tpm_backend_finish_sync(s->be_driver);
    /*
     * we cannot deliver the results to the VM since DMA would touch VM memory
     */

    return 0;
}

static int tpm_spapr_post_load(void *opaque, int version_id)
{
    SpaprTpmState *s = opaque;

    if (s->numbytes) {
        trace_tpm_spapr_post_load();
        /* deliver the results to the VM via DMA */
        tpm_spapr_request_completed(TPM_IF(s), 0);
        s->numbytes = 0;
    }

    return 0;
}

static const VMStateDescription vmstate_spapr_vtpm = {
    .name = "tpm-spapr",
    .pre_save = tpm_spapr_pre_save,
    .post_load = tpm_spapr_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_SPAPR_VIO(vdev, SpaprTpmState),

        VMSTATE_UINT8(state, SpaprTpmState),
        VMSTATE_UINT32(numbytes, SpaprTpmState),
        VMSTATE_VBUFFER_UINT32(buffer, SpaprTpmState, 0, NULL, numbytes),
        /* remember DMA address */
        VMSTATE_UINT32(crq.data, SpaprTpmState),
        VMSTATE_END_OF_LIST(),
    }
};

static const Property tpm_spapr_properties[] = {
    DEFINE_SPAPR_PROPERTIES(SpaprTpmState, vdev),
    DEFINE_PROP_TPMBE("tpmdev", SpaprTpmState, be_driver),
};

static void tpm_spapr_realizefn(SpaprVioDevice *dev, Error **errp)
{
    SpaprTpmState *s = VIO_SPAPR_VTPM(dev);

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    dev->crq.SendFunc = tpm_spapr_do_crq;

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
    s->buffer = g_malloc(TPM_SPAPR_BUFFER_MAX);
}

static void tpm_spapr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SpaprVioDeviceClass *k = VIO_SPAPR_DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    k->realize = tpm_spapr_realizefn;
    k->reset = tpm_spapr_reset;
    k->dt_name = "vtpm";
    k->dt_type = "IBM,vtpm";
    k->get_dt_compatible = tpm_spapr_get_dt_compatible;
    k->signal_mask = 0x00000001;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, tpm_spapr_properties);
    k->rtce_window_size = 0x10000000;
    dc->vmsd = &vmstate_spapr_vtpm;

    tc->model = TPM_MODEL_TPM_SPAPR;
    tc->get_version = tpm_spapr_get_version;
    tc->request_completed = tpm_spapr_request_completed;
}

static const TypeInfo tpm_spapr_info = {
    .name          = TYPE_TPM_SPAPR,
    .parent        = TYPE_VIO_SPAPR_DEVICE,
    .instance_size = sizeof(SpaprTpmState),
    .class_init    = tpm_spapr_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_spapr_register_types(void)
{
    type_register_static(&tpm_spapr_info);
}

type_init(tpm_spapr_register_types)
