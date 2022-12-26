#ifndef HW_ARM_IPOD_TOUCH_AES_H
#define HW_ARM_IPOD_TOUCH_AES_H

#include "qemu/osdep.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"
#include <openssl/aes.h>

#define TYPE_IPOD_TOUCH_AES                "ipodtouch.aes"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchAESState, IPOD_TOUCH_AES)

#define key_uid ((uint8_t[]){0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF})

#define AES_128_CBC_BLOCK_SIZE 64
#define AES_CONTROL 0x0
#define AES_GO 0x4
#define AES_UNKREG0 0x8
#define AES_STATUS 0xC
#define AES_UNKREG1 0x10
#define AES_KEYLEN 0x14
#define AES_INSIZE 0x18
#define AES_INADDR 0x20
#define AES_OUTSIZE 0x24
#define AES_OUTADDR 0x28
#define AES_AUXSIZE 0x2C
#define AES_AUXADDR 0x30
#define AES_SIZE3 0x34
#define AES_KEY_REG 0x4C
#define AES_TYPE 0x6C
#define AES_IV_REG 0x74
#define AES_KEYSIZE 0x20
#define AES_IVSIZE 0x10

typedef enum AESKeyType {
    AESCustom = 0,
    AESGID = 1,
    AESUID = 2
} AESKeyType;

typedef enum AESKeyLen {
    AES128 = 0,
    AES192 = 1,
    AES256 = 2
} AESKeyLen;

typedef struct IPodTouchAESState
{
    SysBusDevice busdev;
    MemoryRegion iomem;
    AES_KEY decryptKey;
	uint32_t ivec[4];
	uint32_t insize;
	uint32_t inaddr;
	uint32_t outsize;
	uint32_t outaddr;
	uint32_t auxaddr;
	uint32_t keytype;
	uint32_t status;
	uint32_t ctrl;
	uint32_t unkreg0;
	uint32_t unkreg1;
	uint32_t operation;
	uint32_t keylen;
	uint32_t custkey[8]; 
} IPodTouchAESState;

#endif
