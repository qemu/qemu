/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device
 */
#include "qemu/osdep.h"
#include "qemu/crc32c.h"
#include "system/dma.h"
#include "migration/vmstate.h"

#include "hw/uefi/var-service.h"
#include "hw/uefi/var-service-api.h"
#include "hw/uefi/var-service-edk2.h"

#include "trace/trace-hw_uefi.h"

static int uefi_vars_pre_load(void *opaque)
{
    uefi_vars_state *uv = opaque;

    uefi_vars_clear_all(uv);
    uefi_vars_policies_clear(uv);
    g_free(uv->buffer);
    return 0;
}

static int uefi_vars_post_load(void *opaque, int version_id)
{
    uefi_vars_state *uv = opaque;

    uefi_vars_update_storage(uv);
    uefi_vars_json_save(uv);
    uv->buffer = g_malloc(uv->buf_size);
    return 0;
}

const VMStateDescription vmstate_uefi_vars = {
    .name = "uefi-vars",
    .pre_load = uefi_vars_pre_load,
    .post_load = uefi_vars_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(sts, uefi_vars_state),
        VMSTATE_UINT32(buf_size, uefi_vars_state),
        VMSTATE_UINT32(buf_addr_lo, uefi_vars_state),
        VMSTATE_UINT32(buf_addr_hi, uefi_vars_state),
        VMSTATE_UINT32(pio_xfer_offset, uefi_vars_state),
        VMSTATE_VBUFFER_ALLOC_UINT32(pio_xfer_buffer, uefi_vars_state,
                                     0, NULL, buf_size),
        VMSTATE_BOOL(end_of_dxe, uefi_vars_state),
        VMSTATE_BOOL(ready_to_boot, uefi_vars_state),
        VMSTATE_BOOL(exit_boot_service, uefi_vars_state),
        VMSTATE_BOOL(policy_locked, uefi_vars_state),
        VMSTATE_UINT64(used_storage, uefi_vars_state),
        VMSTATE_QTAILQ_V(variables, uefi_vars_state, 0,
                         vmstate_uefi_variable, uefi_variable, next),
        VMSTATE_QTAILQ_V(var_policies, uefi_vars_state, 0,
                         vmstate_uefi_var_policy, uefi_var_policy, next),
        VMSTATE_END_OF_LIST()
    },
};

static uint32_t uefi_vars_cmd_mm(uefi_vars_state *uv, bool dma_mode)
{
    hwaddr    dma;
    mm_header *mhdr;
    uint64_t  size;
    uint32_t  retval;

    dma = uv->buf_addr_lo | ((hwaddr)uv->buf_addr_hi << 32);
    mhdr = (mm_header *) uv->buffer;

    if (!uv->buffer || uv->buf_size < sizeof(*mhdr)) {
        return UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE;
    }

    /* read header */
    if (dma_mode) {
        dma_memory_read(&address_space_memory, dma,
                        uv->buffer, sizeof(*mhdr),
                        MEMTXATTRS_UNSPECIFIED);
    } else {
        memcpy(uv->buffer, uv->pio_xfer_buffer, sizeof(*mhdr));
    }

    if (uadd64_overflow(sizeof(*mhdr), mhdr->length, &size)) {
        return UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE;
    }
    if (uv->buf_size < size) {
        return UEFI_VARS_STS_ERR_BAD_BUFFER_SIZE;
    }

    /* read buffer (excl header) */
    if (dma_mode) {
        dma_memory_read(&address_space_memory, dma + sizeof(*mhdr),
                        uv->buffer + sizeof(*mhdr), mhdr->length,
                        MEMTXATTRS_UNSPECIFIED);
    } else {
        memcpy(uv->buffer + sizeof(*mhdr),
               uv->pio_xfer_buffer + sizeof(*mhdr),
               mhdr->length);
    }
    memset(uv->buffer + size, 0, uv->buf_size - size);

    /* dispatch */
    if (qemu_uuid_is_equal(&mhdr->guid, &EfiSmmVariableProtocolGuid)) {
        retval = uefi_vars_mm_vars_proto(uv);

    } else if (qemu_uuid_is_equal(&mhdr->guid, &VarCheckPolicyLibMmiHandlerGuid)) {
        retval = uefi_vars_mm_check_policy_proto(uv);

    } else if (qemu_uuid_is_equal(&mhdr->guid, &EfiEndOfDxeEventGroupGuid)) {
        trace_uefi_event("end-of-dxe");
        uv->end_of_dxe = true;
        retval = UEFI_VARS_STS_SUCCESS;

    } else if (qemu_uuid_is_equal(&mhdr->guid, &EfiEventReadyToBootGuid)) {
        trace_uefi_event("ready-to-boot");
        uv->ready_to_boot = true;
        retval = UEFI_VARS_STS_SUCCESS;

    } else if (qemu_uuid_is_equal(&mhdr->guid, &EfiEventExitBootServicesGuid)) {
        trace_uefi_event("exit-boot-service");
        uv->exit_boot_service = true;
        retval = UEFI_VARS_STS_SUCCESS;

    } else {
        retval = UEFI_VARS_STS_ERR_NOT_SUPPORTED;
    }

    /* write buffer */
    if (dma_mode) {
        dma_memory_write(&address_space_memory, dma,
                         uv->buffer, sizeof(*mhdr) + mhdr->length,
                         MEMTXATTRS_UNSPECIFIED);
    } else {
        memcpy(uv->pio_xfer_buffer + sizeof(*mhdr),
               uv->buffer + sizeof(*mhdr),
               sizeof(*mhdr) + mhdr->length);
    }

    return retval;
}

static void uefi_vars_soft_reset(uefi_vars_state *uv)
{
    g_free(uv->buffer);
    uv->buffer = NULL;
    uv->buf_size = 0;
    uv->buf_addr_lo = 0;
    uv->buf_addr_hi = 0;
}

void uefi_vars_hard_reset(uefi_vars_state *uv)
{
    trace_uefi_hard_reset();
    uefi_vars_soft_reset(uv);

    uv->end_of_dxe        = false;
    uv->ready_to_boot     = false;
    uv->exit_boot_service = false;
    uv->policy_locked     = false;

    uefi_vars_clear_volatile(uv);
    uefi_vars_policies_clear(uv);
    uefi_vars_auth_init(uv);
}

static uint32_t uefi_vars_cmd(uefi_vars_state *uv, uint32_t cmd)
{
    switch (cmd) {
    case UEFI_VARS_CMD_RESET:
        uefi_vars_soft_reset(uv);
        return UEFI_VARS_STS_SUCCESS;
    case UEFI_VARS_CMD_DMA_MM:
        return uefi_vars_cmd_mm(uv, true);
    case UEFI_VARS_CMD_PIO_MM:
        return uefi_vars_cmd_mm(uv, false);
    case UEFI_VARS_CMD_PIO_ZERO_OFFSET:
        uv->pio_xfer_offset = 0;
        return UEFI_VARS_STS_SUCCESS;
    default:
        return UEFI_VARS_STS_ERR_NOT_SUPPORTED;
    }
}

static uint64_t uefi_vars_read(void *opaque, hwaddr addr, unsigned size)
{
    uefi_vars_state *uv = opaque;
    uint64_t retval = -1;
    void *xfer_ptr;

    trace_uefi_reg_read(addr, size);

    switch (addr) {
    case UEFI_VARS_REG_MAGIC:
        retval = UEFI_VARS_MAGIC_VALUE;
        break;
    case UEFI_VARS_REG_CMD_STS:
        retval = uv->sts;
        break;
    case UEFI_VARS_REG_BUFFER_SIZE:
        retval = uv->buf_size;
        break;
    case UEFI_VARS_REG_DMA_BUFFER_ADDR_LO:
        retval = uv->buf_addr_lo;
        break;
    case UEFI_VARS_REG_DMA_BUFFER_ADDR_HI:
        retval = uv->buf_addr_hi;
        break;
    case UEFI_VARS_REG_PIO_BUFFER_TRANSFER:
        if (uv->pio_xfer_offset + size > uv->buf_size) {
            retval = 0;
            break;
        }
        xfer_ptr = uv->pio_xfer_buffer + uv->pio_xfer_offset;
        switch (size) {
        case 1:
            retval = *(uint8_t *)xfer_ptr;
            break;
        case 2:
            retval = *(uint16_t *)xfer_ptr;
            break;
        case 4:
            retval = *(uint32_t *)xfer_ptr;
            break;
        case 8:
            retval = *(uint64_t *)xfer_ptr;
            break;
        }
        uv->pio_xfer_offset += size;
        break;
    case UEFI_VARS_REG_PIO_BUFFER_CRC32C:
        retval = crc32c(0xffffffff, uv->pio_xfer_buffer, uv->pio_xfer_offset);
        break;
    case UEFI_VARS_REG_FLAGS:
        retval = 0;
        if (uv->use_pio) {
            retval |= UEFI_VARS_FLAG_USE_PIO;
        }
    }
    return retval;
}

static void uefi_vars_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uefi_vars_state *uv = opaque;
    void *xfer_ptr;

    trace_uefi_reg_write(addr, val, size);

    switch (addr) {
    case UEFI_VARS_REG_CMD_STS:
        uv->sts = uefi_vars_cmd(uv, val);
        break;
    case UEFI_VARS_REG_BUFFER_SIZE:
        if (val > MAX_BUFFER_SIZE) {
            val = MAX_BUFFER_SIZE;
        }
        uv->buf_size = val;
        g_free(uv->buffer);
        g_free(uv->pio_xfer_buffer);
        uv->buffer = g_malloc(uv->buf_size);
        uv->pio_xfer_buffer = g_malloc(uv->buf_size);
        break;
    case UEFI_VARS_REG_DMA_BUFFER_ADDR_LO:
        uv->buf_addr_lo = val;
        break;
    case UEFI_VARS_REG_DMA_BUFFER_ADDR_HI:
        uv->buf_addr_hi = val;
        break;
    case UEFI_VARS_REG_PIO_BUFFER_TRANSFER:
        if (uv->pio_xfer_offset + size > uv->buf_size) {
            break;
        }
        xfer_ptr = uv->pio_xfer_buffer + uv->pio_xfer_offset;
        switch (size) {
        case 1:
            *(uint8_t *)xfer_ptr = val;
            break;
        case 2:
            *(uint16_t *)xfer_ptr = val;
            break;
        case 4:
            *(uint32_t *)xfer_ptr = val;
            break;
        case 8:
            *(uint64_t *)xfer_ptr = val;
            break;
        }
        uv->pio_xfer_offset += size;
        break;
    case UEFI_VARS_REG_PIO_BUFFER_CRC32C:
    case UEFI_VARS_REG_FLAGS:
    default:
        break;
    }
}

static const MemoryRegionOps uefi_vars_ops = {
    .read = uefi_vars_read,
    .write = uefi_vars_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

void uefi_vars_init(Object *obj, uefi_vars_state *uv)
{
    QTAILQ_INIT(&uv->variables);
    QTAILQ_INIT(&uv->var_policies);
    uv->jsonfd = -1;
    memory_region_init_io(&uv->mr, obj, &uefi_vars_ops, uv,
                          "uefi-vars", UEFI_VARS_REGS_SIZE);
}

void uefi_vars_realize(uefi_vars_state *uv, Error **errp)
{
    uefi_vars_json_init(uv, errp);
    uefi_vars_json_load(uv, errp);
}
