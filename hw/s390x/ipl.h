/*
 * s390 IPL device
 *
 * Copyright 2015, 2020 IBM Corp.
 * Author(s): Zhang Fan <bjfanzh@cn.ibm.com>
 * Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef HW_S390_IPL_H
#define HW_S390_IPL_H

#include "cpu.h"
#include "exec/target_page.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "hw/qdev-core.h"
#include "hw/s390x/ipl/qipl.h"
#include "qom/object.h"
#include "target/s390x/kvm/pv.h"

#define DIAG308_FLAGS_LP_VALID 0x80
#define MAX_BOOT_DEVS 8 /* Max number of devices that may have a bootindex */

void s390_ipl_convert_loadparm(char *ascii_lp, uint8_t *ebcdic_lp);
void s390_ipl_fmt_loadparm(uint8_t *loadparm, char *str, Error **errp);
void s390_rebuild_iplb(uint16_t index, IplParameterBlock *iplb);
void s390_ipl_update_diag308(IplParameterBlock *iplb);
int s390_ipl_prepare_pv_header(struct S390PVResponse *pv_resp,
                               Error **errp);
int s390_ipl_pv_unpack(struct S390PVResponse *pv_resp);
void s390_ipl_prepare_cpu(S390CPU *cpu);
IplParameterBlock *s390_ipl_get_iplb(void);
IplParameterBlock *s390_ipl_get_iplb_pv(void);

enum s390_reset {
    /* default is a reset not triggered by a CPU e.g. issued by QMP */
    S390_RESET_EXTERNAL = 0,
    S390_RESET_REIPL,
    S390_RESET_MODIFIED_CLEAR,
    S390_RESET_LOAD_NORMAL,
    S390_RESET_PV,
};
void s390_ipl_reset_request(CPUState *cs, enum s390_reset reset_type);
void s390_ipl_get_reset_request(CPUState **cs, enum s390_reset *reset_type);
void s390_ipl_clear_reset_request(void);

#define QIPL_ADDRESS  0xcc

/* Boot Menu flags */
#define QIPL_FLAG_BM_OPTS_CMD   0x80
#define QIPL_FLAG_BM_OPTS_ZIPL  0x40

#define TYPE_S390_IPL "s390-ipl"
OBJECT_DECLARE_SIMPLE_TYPE(S390IPLState, S390_IPL)

struct S390IPLState {
    /*< private >*/
    DeviceState parent_obj;
    IplParameterBlock iplb;
    IplParameterBlock iplb_pv;
    QemuIplParameters qipl;
    uint64_t start_addr;
    uint64_t compat_start_addr;
    uint64_t bios_start_addr;
    uint64_t compat_bios_start_addr;
    bool enforce_bios;
    bool iplb_valid;
    bool iplb_valid_pv;
    bool rebuilt_iplb;
    uint16_t iplb_index;
    /* reset related properties don't have to be migrated or reset */
    enum s390_reset reset_type;
    int reset_cpu_index;

    /*< public >*/
    char *kernel;
    char *initrd;
    char *cmdline;
    char *firmware;
    uint8_t cssid;
    uint8_t ssid;
    uint16_t devno;
};
QEMU_BUILD_BUG_MSG(offsetof(S390IPLState, iplb) & 3, "alignment of iplb wrong");

#define DIAG_308_RC_OK              0x0001
#define DIAG_308_RC_NO_CONF         0x0102
#define DIAG_308_RC_INVALID         0x0402
#define DIAG_308_RC_NO_PV_CONF      0x0902
#define DIAG_308_RC_INVAL_FOR_PV    0x0a02

#define DIAG308_RESET_MOD_CLR       0
#define DIAG308_RESET_LOAD_NORM     1
#define DIAG308_LOAD_CLEAR          3
#define DIAG308_LOAD_NORMAL_DUMP    4
#define DIAG308_SET                 5
#define DIAG308_STORE               6
#define DIAG308_PV_SET              8
#define DIAG308_PV_STORE            9
#define DIAG308_PV_START            10

#define S390_IPL_TYPE_FCP 0x00
#define S390_IPL_TYPE_CCW 0x02
#define S390_IPL_TYPE_PV 0x05
#define S390_IPL_TYPE_QEMU_SCSI 0xff

#define S390_IPLB_HEADER_LEN 8
#define S390_IPLB_MIN_PV_LEN 148
#define S390_IPLB_MIN_CCW_LEN 200
#define S390_IPLB_MIN_FCP_LEN 384
#define S390_IPLB_MIN_QEMU_SCSI_LEN 200

static inline bool iplb_valid_len(IplParameterBlock *iplb)
{
    return be32_to_cpu(iplb->len) <= sizeof(IplParameterBlock);
}

static inline bool ipl_valid_pv_components(IplParameterBlock *iplb)
{
    IPLBlockPV *ipib_pv = &iplb->pv;
    int i;

    if (ipib_pv->num_comp == 0) {
        return false;
    }

    for (i = 0; i < ipib_pv->num_comp; i++) {
        /* Addr must be 4k aligned */
        if (ipib_pv->components[i].addr & ~TARGET_PAGE_MASK) {
            return false;
        }

        /* Tweak prefix is monotonically increasing with each component */
        if (i < ipib_pv->num_comp - 1 &&
            ipib_pv->components[i].tweak_pref >=
            ipib_pv->components[i + 1].tweak_pref) {
            return false;
        }
    }
    return true;
}

static inline bool ipl_valid_pv_header(IplParameterBlock *iplb)
{
        IPLBlockPV *ipib_pv = &iplb->pv;

        if (ipib_pv->pv_header_len > 2 * TARGET_PAGE_SIZE) {
            return false;
        }

        if (!address_space_access_valid(&address_space_memory,
                                        ipib_pv->pv_header_addr,
                                        ipib_pv->pv_header_len,
                                        false,
                                        MEMTXATTRS_UNSPECIFIED)) {
            return false;
        }

        return true;
}

static inline bool iplb_valid_pv(IplParameterBlock *iplb)
{
    if (iplb->pbt != S390_IPL_TYPE_PV ||
        be32_to_cpu(iplb->len) < S390_IPLB_MIN_PV_LEN) {
        return false;
    }
    if (!ipl_valid_pv_header(iplb)) {
        return false;
    }
    return ipl_valid_pv_components(iplb);
}

static inline bool iplb_valid(IplParameterBlock *iplb)
{
    uint32_t len = be32_to_cpu(iplb->len);

    switch (iplb->pbt) {
    case S390_IPL_TYPE_FCP:
        return len >= S390_IPLB_MIN_FCP_LEN;
    case S390_IPL_TYPE_CCW:
        return len >= S390_IPLB_MIN_CCW_LEN;
    case S390_IPL_TYPE_QEMU_SCSI:
    default:
        return false;
    }
}

#endif
