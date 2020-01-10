/*
 * SPAPR TPM Proxy/Hypercall
 *
 * Copyright IBM Corp. 2019
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "sysemu/reset.h"
#include "cpu.h"
#include "hw/ppc/spapr.h"
#include "hw/qdev-properties.h"
#include "trace.h"

#define TPM_SPAPR_BUFSIZE 4096

enum {
    TPM_COMM_OP_EXECUTE = 1,
    TPM_COMM_OP_CLOSE_SESSION = 2,
};

static void spapr_tpm_proxy_reset(void *opaque)
{
    SpaprTpmProxy *tpm_proxy = SPAPR_TPM_PROXY(opaque);

    if (tpm_proxy->host_fd != -1) {
        close(tpm_proxy->host_fd);
        tpm_proxy->host_fd = -1;
    }
}

static ssize_t tpm_execute(SpaprTpmProxy *tpm_proxy, target_ulong *args)
{
    uint64_t data_in = ppc64_phys_to_real(args[1]);
    target_ulong data_in_size = args[2];
    uint64_t data_out = ppc64_phys_to_real(args[3]);
    target_ulong data_out_size = args[4];
    uint8_t buf_in[TPM_SPAPR_BUFSIZE];
    uint8_t buf_out[TPM_SPAPR_BUFSIZE];
    ssize_t ret;

    trace_spapr_tpm_execute(data_in, data_in_size, data_out, data_out_size);

    if (data_in_size > TPM_SPAPR_BUFSIZE) {
        error_report("invalid TPM input buffer size: " TARGET_FMT_lu,
                     data_in_size);
        return H_P3;
    }

    if (data_out_size < TPM_SPAPR_BUFSIZE) {
        error_report("invalid TPM output buffer size: " TARGET_FMT_lu,
                     data_out_size);
        return H_P5;
    }

    if (tpm_proxy->host_fd == -1) {
        tpm_proxy->host_fd = open(tpm_proxy->host_path, O_RDWR);
        if (tpm_proxy->host_fd == -1) {
            error_report("failed to open TPM device %s: %d",
                         tpm_proxy->host_path, errno);
            return H_RESOURCE;
        }
    }

    cpu_physical_memory_read(data_in, buf_in, data_in_size);

    do {
        ret = write(tpm_proxy->host_fd, buf_in, data_in_size);
        if (ret > 0) {
            data_in_size -= ret;
        }
    } while ((ret >= 0 && data_in_size > 0) || (ret == -1 && errno == EINTR));

    if (ret == -1) {
        error_report("failed to write to TPM device %s: %d",
                     tpm_proxy->host_path, errno);
        return H_RESOURCE;
    }

    do {
        ret = read(tpm_proxy->host_fd, buf_out, data_out_size);
    } while (ret == 0 || (ret == -1 && errno == EINTR));

    if (ret == -1) {
        error_report("failed to read from TPM device %s: %d",
                     tpm_proxy->host_path, errno);
        return H_RESOURCE;
    }

    cpu_physical_memory_write(data_out, buf_out, ret);
    args[0] = ret;

    return H_SUCCESS;
}

static target_ulong h_tpm_comm(PowerPCCPU *cpu,
                               SpaprMachineState *spapr,
                               target_ulong opcode,
                               target_ulong *args)
{
    target_ulong op = args[0];
    SpaprTpmProxy *tpm_proxy = spapr->tpm_proxy;

    if (!tpm_proxy) {
        error_report("TPM proxy not available");
        return H_FUNCTION;
    }

    trace_spapr_h_tpm_comm(tpm_proxy->host_path, op);

    switch (op) {
    case TPM_COMM_OP_EXECUTE:
        return tpm_execute(tpm_proxy, args);
    case TPM_COMM_OP_CLOSE_SESSION:
        spapr_tpm_proxy_reset(tpm_proxy);
        return H_SUCCESS;
    default:
        return H_PARAMETER;
    }
}

static void spapr_tpm_proxy_realize(DeviceState *d, Error **errp)
{
    SpaprTpmProxy *tpm_proxy = SPAPR_TPM_PROXY(d);

    if (tpm_proxy->host_path == NULL) {
        error_setg(errp, "must specify 'host-path' option for device");
        return;
    }

    tpm_proxy->host_fd = -1;
    qemu_register_reset(spapr_tpm_proxy_reset, tpm_proxy);
}

static void spapr_tpm_proxy_unrealize(DeviceState *d, Error **errp)
{
    SpaprTpmProxy *tpm_proxy = SPAPR_TPM_PROXY(d);

    qemu_unregister_reset(spapr_tpm_proxy_reset, tpm_proxy);
}

static Property spapr_tpm_proxy_properties[] = {
    DEFINE_PROP_STRING("host-path", SpaprTpmProxy, host_path),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_tpm_proxy_class_init(ObjectClass *k, void *data)
{
    DeviceClass *dk = DEVICE_CLASS(k);

    dk->realize = spapr_tpm_proxy_realize;
    dk->unrealize = spapr_tpm_proxy_unrealize;
    dk->user_creatable = true;
    device_class_set_props(dk, spapr_tpm_proxy_properties);
}

static const TypeInfo spapr_tpm_proxy_info = {
    .name          = TYPE_SPAPR_TPM_PROXY,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(SpaprTpmProxy),
    .class_init    = spapr_tpm_proxy_class_init,
};

static void spapr_tpm_proxy_register_types(void)
{
    type_register_static(&spapr_tpm_proxy_info);
    spapr_register_hypercall(SVM_H_TPM_COMM, h_tpm_comm);
}

type_init(spapr_tpm_proxy_register_types)
