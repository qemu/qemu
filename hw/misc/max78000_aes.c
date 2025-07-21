/*
 * MAX78000 AES
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/max78000_aes.h"
#include "crypto/aes.h"

static void max78000_aes_set_status(Max78000AesState *s)
{
    s->status = 0;
    if (s->result_index >= 16) {
        s->status |= OUTPUT_FULL;
    }
    if (s->result_index == 0) {
        s->status |= OUTPUT_EMPTY;
    }
    if (s->data_index >= 16) {
        s->status |= INPUT_FULL;
    }
    if (s->data_index == 0) {
        s->status |= INPUT_EMPTY;
    }
}

static uint64_t max78000_aes_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max78000AesState *s = opaque;
    switch (addr) {
    case CTRL:
        return s->ctrl;

    case STATUS:
        return s->status;

    case INTFL:
        return s->intfl;

    case INTEN:
        return s->inten;

    case FIFO:
        if (s->result_index >= 4) {
            s->intfl &= ~DONE;
            s->result_index -= 4;
            max78000_aes_set_status(s);
            return ldl_be_p(&s->result[s->result_index]);
        } else{
            return 0;
        }

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
        break;

    }
    return 0;
}

static void max78000_aes_do_crypto(Max78000AesState *s)
{
    int keylen = 256;
    uint8_t *keydata = s->key;
    if ((s->ctrl & KEY_SIZE) == 0) {
        keylen = 128;
        keydata += 16;
    } else if ((s->ctrl & KEY_SIZE) == 1 << 6) {
        keylen = 192;
        keydata += 8;
    }

    /*
     * The MAX78000 AES engine stores an internal key, which it uses only
     * for decryption. This results in the slighly odd looking pairs of
     * set_encrypt and set_decrypt calls below; s->internal_key is
     * being stored for later use in both cases.
     */
    AES_KEY key;
    if ((s->ctrl & TYPE) == 0) {
        AES_set_encrypt_key(keydata, keylen, &key);
        AES_set_decrypt_key(keydata, keylen, &s->internal_key);
        AES_encrypt(s->data, s->result, &key);
        s->result_index = 16;
    } else if ((s->ctrl & TYPE) == 1 << 8) {
        AES_set_decrypt_key(keydata, keylen, &key);
        AES_set_decrypt_key(keydata, keylen, &s->internal_key);
        AES_decrypt(s->data, s->result, &key);
        s->result_index = 16;
    } else{
        AES_decrypt(s->data, s->result, &s->internal_key);
        s->result_index = 16;
    }
    s->intfl |= DONE;
}

static void max78000_aes_write(void *opaque, hwaddr addr,
                    uint64_t val64, unsigned int size)
{
    Max78000AesState *s = opaque;
    uint32_t val = val64;
    switch (addr) {
    case CTRL:
        if (val & OUTPUT_FLUSH) {
            s->result_index = 0;
            val &= ~OUTPUT_FLUSH;
        }
        if (val & INPUT_FLUSH) {
            s->data_index = 0;
            val &= ~INPUT_FLUSH;
        }
        if (val & START) {
            max78000_aes_do_crypto(s);
        }

        /* Hardware appears to stay enabled even if 0 written */
        s->ctrl = val | (s->ctrl & AES_EN);
        break;

    case FIFO:
        assert(s->data_index <= 12);
        stl_be_p(&s->data[12 - s->data_index], val);
        s->data_index += 4;
        if (s->data_index >= 16) {
            s->data_index = 0;
            max78000_aes_do_crypto(s);
        }
        break;

    case KEY_BASE ... KEY_END - 4:
        stl_be_p(&s->key[(KEY_END - KEY_BASE - 4) - (addr - KEY_BASE)], val);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
        break;

    }
    max78000_aes_set_status(s);
}

static void max78000_aes_reset_hold(Object *obj, ResetType type)
{
    Max78000AesState *s = MAX78000_AES(obj);
    s->ctrl = 0;
    s->status = 0;
    s->intfl = 0;
    s->inten = 0;

    s->data_index = 0;
    s->result_index = 0;

    memset(s->data, 0, sizeof(s->data));
    memset(s->key, 0, sizeof(s->key));
    memset(s->result, 0, sizeof(s->result));
    memset(&s->internal_key, 0, sizeof(s->internal_key));
}

static const MemoryRegionOps max78000_aes_ops = {
    .read = max78000_aes_read,
    .write = max78000_aes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_max78000_aes = {
    .name = TYPE_MAX78000_AES,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, Max78000AesState),
        VMSTATE_UINT32(status, Max78000AesState),
        VMSTATE_UINT32(intfl, Max78000AesState),
        VMSTATE_UINT32(inten, Max78000AesState),
        VMSTATE_UINT8_ARRAY(data, Max78000AesState, 16),
        VMSTATE_UINT8_ARRAY(key, Max78000AesState, 32),
        VMSTATE_UINT8_ARRAY(result, Max78000AesState, 16),
        VMSTATE_UINT32_ARRAY(internal_key.rd_key, Max78000AesState, 60),
        VMSTATE_INT32(internal_key.rounds, Max78000AesState),
        VMSTATE_END_OF_LIST()
    }
};

static void max78000_aes_init(Object *obj)
{
    Max78000AesState *s = MAX78000_AES(obj);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &max78000_aes_ops, s,
                        TYPE_MAX78000_AES, 0xc00);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

}

static void max78000_aes_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    rc->phases.hold = max78000_aes_reset_hold;
    dc->vmsd = &vmstate_max78000_aes;

}

static const TypeInfo max78000_aes_info = {
    .name          = TYPE_MAX78000_AES,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max78000AesState),
    .instance_init = max78000_aes_init,
    .class_init    = max78000_aes_class_init,
};

static void max78000_aes_register_types(void)
{
    type_register_static(&max78000_aes_info);
}

type_init(max78000_aes_register_types)
