#include "qemu/osdep.h"
#include "hw/usb.h"
#include "desc.h"

/*
 * Microsoft OS Descriptors
 *
 * Windows tries to fetch some special descriptors with informations
 * specifically for windows.  Presence is indicated using a special
 * string @ index 0xee.  There are two kinds of descriptors:
 *
 * compatid descriptor
 *   Used to bind drivers, if usb class isn't specific enougth.
 *   Used for PTP/MTP for example (both share the same usb class).
 *
 * properties descriptor
 *   Does carry registry entries.  They show up in
 *   HLM\SYSTEM\CurrentControlSet\Enum\USB\<devid>\<serial>\Device Parameters
 *
 * Note that Windows caches the stuff it got in the registry, so when
 * playing with this you have to delete registry subtrees to make
 * windows query the device again:
 *   HLM\SYSTEM\CurrentControlSet\Control\usbflags
 *   HLM\SYSTEM\CurrentControlSet\Enum\USB
 * Windows will complain it can't delete entries on the second one.
 * It has deleted everything it had permissions too, which is enouth
 * as this includes "Device Parameters".
 *
 * http://msdn.microsoft.com/en-us/library/windows/hardware/ff537430.aspx
 *
 */

/* ------------------------------------------------------------------ */

typedef struct msos_compat_hdr {
    uint32_t dwLength;
    uint8_t  bcdVersion_lo;
    uint8_t  bcdVersion_hi;
    uint8_t  wIndex_lo;
    uint8_t  wIndex_hi;
    uint8_t  bCount;
    uint8_t  reserved[7];
} QEMU_PACKED msos_compat_hdr;

typedef struct msos_compat_func {
    uint8_t  bFirstInterfaceNumber;
    uint8_t  reserved_1;
    char     compatibleId[8];
    uint8_t  subCompatibleId[8];
    uint8_t  reserved_2[6];
} QEMU_PACKED msos_compat_func;

static int usb_desc_msos_compat(const USBDesc *desc, uint8_t *dest)
{
    msos_compat_hdr *hdr = (void *)dest;
    msos_compat_func *func;
    int length = sizeof(*hdr);
    int count = 0;

    func = (void *)(dest + length);
    func->bFirstInterfaceNumber = 0;
    func->reserved_1 = 0x01;
    if (desc->msos->CompatibleID) {
        snprintf(func->compatibleId, sizeof(func->compatibleId),
                 "%s", desc->msos->CompatibleID);
    }
    length += sizeof(*func);
    count++;

    hdr->dwLength      = cpu_to_le32(length);
    hdr->bcdVersion_lo = 0x00;
    hdr->bcdVersion_hi = 0x01;
    hdr->wIndex_lo     = 0x04;
    hdr->wIndex_hi     = 0x00;
    hdr->bCount        = count;
    return length;
}

/* ------------------------------------------------------------------ */

typedef struct msos_prop_hdr {
    uint32_t dwLength;
    uint8_t  bcdVersion_lo;
    uint8_t  bcdVersion_hi;
    uint8_t  wIndex_lo;
    uint8_t  wIndex_hi;
    uint8_t  wCount_lo;
    uint8_t  wCount_hi;
} QEMU_PACKED msos_prop_hdr;

typedef struct msos_prop {
    uint32_t dwLength;
    uint32_t dwPropertyDataType;
    uint8_t  dwPropertyNameLength_lo;
    uint8_t  dwPropertyNameLength_hi;
    uint8_t  bPropertyName[];
} QEMU_PACKED msos_prop;

typedef struct msos_prop_data {
    uint32_t dwPropertyDataLength;
    uint8_t  bPropertyData[];
} QEMU_PACKED msos_prop_data;

typedef enum msos_prop_type {
    MSOS_REG_SZ        = 1,
    MSOS_REG_EXPAND_SZ = 2,
    MSOS_REG_BINARY    = 3,
    MSOS_REG_DWORD_LE  = 4,
    MSOS_REG_DWORD_BE  = 5,
    MSOS_REG_LINK      = 6,
    MSOS_REG_MULTI_SZ  = 7,
} msos_prop_type;

static int usb_desc_msos_prop_name(struct msos_prop *prop,
                                   const wchar_t *name)
{
    int length = wcslen(name) + 1;
    int i;

    prop->dwPropertyNameLength_lo = usb_lo(length*2);
    prop->dwPropertyNameLength_hi = usb_hi(length*2);
    for (i = 0; i < length; i++) {
        prop->bPropertyName[i*2]   = usb_lo(name[i]);
        prop->bPropertyName[i*2+1] = usb_hi(name[i]);
    }
    return length*2;
}

static int usb_desc_msos_prop_str(uint8_t *dest, msos_prop_type type,
                                  const wchar_t *name, const wchar_t *value)
{
    struct msos_prop *prop = (void *)dest;
    struct msos_prop_data *data;
    int length = sizeof(*prop);
    int i, vlen = wcslen(value) + 1;

    prop->dwPropertyDataType = cpu_to_le32(type);
    length += usb_desc_msos_prop_name(prop, name);
    data = (void *)(dest + length);

    data->dwPropertyDataLength = cpu_to_le32(vlen*2);
    length += sizeof(*prop);

    for (i = 0; i < vlen; i++) {
        data->bPropertyData[i*2]   = usb_lo(value[i]);
        data->bPropertyData[i*2+1] = usb_hi(value[i]);
    }
    length += vlen*2;

    prop->dwLength = cpu_to_le32(length);
    return length;
}

static int usb_desc_msos_prop_dword(uint8_t *dest, const wchar_t *name,
                                    uint32_t value)
{
    struct msos_prop *prop = (void *)dest;
    struct msos_prop_data *data;
    int length = sizeof(*prop);

    prop->dwPropertyDataType = cpu_to_le32(MSOS_REG_DWORD_LE);
    length += usb_desc_msos_prop_name(prop, name);
    data = (void *)(dest + length);

    data->dwPropertyDataLength = cpu_to_le32(4);
    data->bPropertyData[0] = (value)       & 0xff;
    data->bPropertyData[1] = (value >>  8) & 0xff;
    data->bPropertyData[2] = (value >> 16) & 0xff;
    data->bPropertyData[3] = (value >> 24) & 0xff;
    length += sizeof(*prop) + 4;

    prop->dwLength = cpu_to_le32(length);
    return length;
}

static int usb_desc_msos_prop(const USBDesc *desc, uint8_t *dest)
{
    msos_prop_hdr *hdr = (void *)dest;
    int length = sizeof(*hdr);
    int count = 0;

    if (desc->msos->Label) {
        /*
         * Given as example in the specs.  Havn't figured yet where
         * this label shows up in the windows gui.
         */
        length += usb_desc_msos_prop_str(dest+length, MSOS_REG_SZ,
                                         L"Label", desc->msos->Label);
        count++;
    }

    if (desc->msos->SelectiveSuspendEnabled) {
        /*
         * Signaling remote wakeup capability in the standard usb
         * descriptors isn't enouth to make windows actually use it.
         * This is the "Yes, we really mean it" registy entry to flip
         * the switch in the windows drivers.
         */
        length += usb_desc_msos_prop_dword(dest+length,
                                           L"SelectiveSuspendEnabled", 1);
        count++;
    }

    hdr->dwLength      = cpu_to_le32(length);
    hdr->bcdVersion_lo = 0x00;
    hdr->bcdVersion_hi = 0x01;
    hdr->wIndex_lo     = 0x05;
    hdr->wIndex_hi     = 0x00;
    hdr->wCount_lo     = usb_lo(count);
    hdr->wCount_hi     = usb_hi(count);
    return length;
}

/* ------------------------------------------------------------------ */

int usb_desc_msos(const USBDesc *desc,  USBPacket *p,
                  int index, uint8_t *dest, size_t len)
{
    void *buf = g_malloc0(4096);
    int length = 0;

    switch (index) {
    case 0x0004:
        length = usb_desc_msos_compat(desc, buf);
        break;
    case 0x0005:
        length = usb_desc_msos_prop(desc, buf);
        break;
    }

    if (length > len) {
        length = len;
    }
    memcpy(dest, buf, length);
    g_free(buf);

    p->actual_length = length;
    return 0;
}
