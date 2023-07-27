#include "hw/arm/ipod_touch_aes.h"

static uint64_t ipod_touch_aes_read(void *opaque, hwaddr offset, unsigned size)
{
    struct IPodTouchAESState *aesop = (struct IPodTouchAESState *)opaque;

    switch(offset) {
        case AES_STATUS:
            return aesop->status;
      default:
            //fprintf(stderr, "%s: UNMAPPED AES_ADDR @ offset 0x%08x\n", __FUNCTION__, offset);
            break;
    }

    return 0;
}

static void ipod_touch_aes_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    struct IPodTouchAESState *aesop = (struct IPodTouchAESState *)opaque;

    uint8_t *inbuf;
    uint8_t *buf;

    // fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);
    uint32_t *data = malloc(sizeof(uint16_t) * 9);

    switch(offset) {
        case AES_GO:
            // PATCH OUT
            // in iBoot, we also patch the length check after LZSS compression since that can lead to issues

            inbuf = (uint8_t *)malloc(aesop->insize);
            cpu_physical_memory_read((aesop->inaddr), inbuf, aesop->insize);

            switch(aesop->keytype) {
                    case AESGID:
                        break;         
                    case AESUID:
                        AES_set_decrypt_key(key_uid, sizeof(key_uid) * 8, &aesop->decryptKey);
                        break;
                    case AESCustom:
                        AES_set_decrypt_key((uint8_t *)(&aesop->custkey[4]), 0x10 * 8, &aesop->decryptKey);
                        break;
            }

            buf = (uint8_t *) malloc(aesop->insize);
            printf("In size: %d, out size: %d, in addr: 0x%08x, in buf: 0x%08x, out addr: 0x%08x, %d\n", aesop->insize, aesop->outsize, aesop->inaddr, ((uint32_t *)inbuf)[0], aesop->outaddr, aesop->gid_encryption_count);

            if(aesop->keytype == AESGID) {
                // Unfortunately, we don't have access to the GID key.
                // However, we know that when the AES engine is invoked with the GID key type, it's for the decryption of the IMG file.
                // Instead, we provide an hard-coded key as result and copy it to the output buffer.
                // source: https://www.theiphonewiki.com/wiki/Sugar_Bowl_5F138_(iPod2,1)
                if(aesop->gid_encryption_count == 0) { // LLB
                    char key[] = { 
                        0xce, 0x97, 0xa7, 0xc8, 0x2e, 0xf8, 0x64, 0x67, 0x5e, 0xd3, 0x68, 0x05, 0x97, 0xec, 0x2a, 0xef, // IV
                        0x27, 0x73, 0x2a, 0x6b, 0xbf, 0xb1, 0x4a, 0x07, 0x25, 0x0a, 0x2e, 0x46, 0x82, 0xbf, 0x3c, 0xba, // key
                    };
                    for(int i = 0; i < aesop->insize; i++) { buf[i] = key[i]; }
                }
                else if(aesop->gid_encryption_count == 1) { // iBoot
                    char key[] = { 
                        0xb3, 0x63, 0x3a, 0xfb, 0xe0, 0x2e, 0x0e, 0x9b, 0xa4, 0xd7, 0x36, 0x6c, 0x47, 0xab, 0xe5, 0xa8, // IV
                        0x2d, 0x91, 0x6d, 0xab, 0xb6, 0xdf, 0xd4, 0x59, 0x4d, 0xbe, 0x36, 0x35, 0xb4, 0xc7, 0x16, 0x62, // key
                    };
                    for(int i = 0; i < aesop->insize; i++) { buf[i] = key[i]; }
                }
                else if(aesop->gid_encryption_count == 2) { // apple logo

                    // very ugly - we patch out here the LZSS check
                    data[0] = 0x0; // NOP
                    cpu_physical_memory_write(0x0ff119f0, (uint8_t *)data, 4);

                    char key[] = { 
                        0x64, 0x23, 0x8f, 0xb0, 0x32, 0x91, 0x42, 0x25, 0x22, 0xb5, 0xdd, 0x28, 0x3f, 0xc3, 0x89, 0x5c, // IV
                        0x85, 0x9f, 0xd4, 0xd3, 0x82, 0xb8, 0x38, 0x51, 0x56, 0xfc, 0x58, 0x1a, 0x7f, 0x1d, 0x97, 0x22, // key
                    };
                    for(int i = 0; i < aesop->insize; i++) { buf[i] = key[i]; }
                }
                else if(aesop->gid_encryption_count == 3) { // kernelcache
                    char key[] = { 
                        0xa1, 0x91, 0x29, 0x12, 0x90, 0xd4, 0x87, 0xff, 0x07, 0x31, 0x96, 0x9c, 0x5f, 0xc8, 0xd9, 0x18, // IV
                        0x0e, 0x4d, 0x23, 0xfa, 0x67, 0x59, 0x99, 0xd5, 0x95, 0x9d, 0xd1, 0x0c, 0x8d, 0xd7, 0x3d, 0x20, // key
                    };
                    for(int i = 0; i < aesop->insize; i++) { buf[i] = key[i]; }
                }
                else if(aesop->gid_encryption_count == 4) { // device tree
                    char key[] = { 
                        0xcc, 0xff, 0x63, 0x4e, 0xe1, 0x27, 0x35, 0xf0, 0x19, 0x16, 0xc4, 0xa6, 0xb2, 0x0f, 0xf1, 0x45, // IV
                        0xe1, 0x7b, 0xcd, 0x56, 0x8d, 0xf1, 0xcd, 0xdc, 0x8f, 0xec, 0xbf, 0x54, 0x87, 0xd5, 0xc3, 0xce, // key
                    };
                    for(int i = 0; i < aesop->insize; i++) { buf[i] = key[i]; }
                }

                aesop->gid_encryption_count++;
            }
            else {
                AES_cbc_encrypt(inbuf, buf, aesop->insize, &aesop->decryptKey, (uint8_t *)aesop->ivec, AES_DECRYPT);
            }

            if(aesop->outaddr != 0x220100ac && aesop->outaddr != 0x0bf08468 && aesop->outaddr != 0x0fb9bcdc) { // TODO very ugly hack - for the RSA key decryption, it seems that doing nothing results in the correct decryption key??
                // BUG: after decrypting the kernel, we update the Adler CRC code and number of expected bytes.
                if(aesop->outaddr == 0x0b000020) {
                    uint32_t *cast_buf = (uint32_t *)buf;
                    cast_buf[2] = 0xA7886041; // adler
                    cast_buf[3] = 0xF5D37E00; // 8311797 in big endian
                }

                // BUG: after decrypting the device tree, the last few bytes are incorrect
                if(aesop->outaddr == 0x0bf00020) {
                    uint32_t *cast_buf = (uint32_t *)buf;
                    cast_buf[8429] = 0x4; // setting the size of the AAPL,phandle property to 4
                    cast_buf[8430] = 0x0011C4F0; // set the right handle
                }

                cpu_physical_memory_write((aesop->outaddr), buf, aesop->insize);
            }

            memset(aesop->custkey, 0, 0x20);
            memset(aesop->ivec, 0, 0x10);
            free(inbuf);
            free(buf);
            aesop->outsize = aesop->insize;
            aesop->status = 0xf;
            break;
        case AES_KEYLEN:
            aesop->operation = value;
            aesop->keylen = value;
            break;
        case AES_INADDR:
            aesop->inaddr = value;
            break;
        case AES_INSIZE:
            aesop->insize = value;
            break;
        case AES_OUTSIZE:
            aesop->outsize = value;
            break;
        case AES_OUTADDR:
            aesop->outaddr = value;
            break;
        case AES_TYPE:
            aesop->keytype = value;
            break;
        case AES_KEY_REG ... ((AES_KEY_REG + AES_KEYSIZE) - 1):
            {
                uint8_t idx = (offset - AES_KEY_REG) / 4;
                aesop->custkey[idx] |= value;
                break;
            }
        case AES_IV_REG ... ((AES_IV_REG + AES_IVSIZE) -1 ):
            {
                uint8_t idx = (offset - AES_IV_REG) / 4;
                aesop->ivec[idx] |= value;
                break;
            }
        default:
            //fprintf(stderr, "%s: UNMAPPED AES_ADDR @ offset 0x%08x - 0x%08x\n", __FUNCTION__, offset, value);
            break;
    }
}

static const MemoryRegionOps aes_ops = {
    .read = ipod_touch_aes_read,
    .write = ipod_touch_aes_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_aes_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchAESState *s = IPOD_TOUCH_AES(dev);

    memory_region_init_io(&s->iomem, obj, &aes_ops, s, "aes", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    memset(&s->custkey, 0, 8 * sizeof(uint32_t));
    memset(&s->ivec, 0, 4 * sizeof(uint32_t));

    s->gid_encryption_count = 0;
}

static void ipod_touch_aes_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_touch_aes_info = {
    .name          = TYPE_IPOD_TOUCH_AES,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchAESState),
    .instance_init = ipod_touch_aes_init,
    .class_init    = ipod_touch_aes_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_aes_info);
}

type_init(ipod_touch_machine_types)