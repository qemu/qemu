/*
 * QEMU AMD PC-Net II (Am79C970A) emulation
 *
 * Copyright (c) 2004 Antony T Curtis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* This software was written to be compatible with the specification:
 * AMD Am79C970A PCnet-PCI II Ethernet Controller Data-Sheet
 * AMD Publication# 19436  Rev:E  Amendment/0  Issue Date: June 2000
 */

/*
 * On Sparc32, this is the Lance (Am7990) part of chip STP2000 (Master I/O), also
 * produced as NCR89C100. See
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR89C100.txt
 * and
 * http://www.ibiblio.org/pub/historic-linux/early-ports/Sparc/NCR/NCR92C990.txt
 */

#include "sysbus.h"
#include "pci.h"
#include "net.h"
#include "qemu-timer.h"
#include "qemu_socket.h"

//#define PCNET_DEBUG
//#define PCNET_DEBUG_IO
//#define PCNET_DEBUG_BCR
//#define PCNET_DEBUG_CSR
//#define PCNET_DEBUG_RMD
//#define PCNET_DEBUG_TMD
//#define PCNET_DEBUG_MATCH


#define PCNET_IOPORT_SIZE       0x20
#define PCNET_PNPMMIO_SIZE      0x20

#define PCNET_LOOPTEST_CRC	1
#define PCNET_LOOPTEST_NOCRC	2


typedef struct PCNetState_st PCNetState;

struct PCNetState_st {
    PCIDevice *pci_dev;
    VLANClientState *vc;
    uint8_t macaddr[6];
    QEMUTimer *poll_timer;
    int rap, isr, lnkst;
    uint32_t rdra, tdra;
    uint8_t prom[16];
    uint16_t csr[128];
    uint16_t bcr[32];
    uint64_t timer;
    int mmio_index, xmit_pos, recv_pos;
    uint8_t buffer[4096];
    int tx_busy;
    qemu_irq irq;
    void (*phys_mem_read)(void *dma_opaque, target_phys_addr_t addr,
                         uint8_t *buf, int len, int do_bswap);
    void (*phys_mem_write)(void *dma_opaque, target_phys_addr_t addr,
                          uint8_t *buf, int len, int do_bswap);
    void *dma_opaque;
    int looptest;
};

typedef struct {
    PCIDevice pci_dev;
    PCNetState state;
} PCIPCNetState;

typedef struct {
    SysBusDevice busdev;
    PCNetState state;
} SysBusPCNetState;

struct qemu_ether_header {
    uint8_t ether_dhost[6];
    uint8_t ether_shost[6];
    uint16_t ether_type;
};

/* BUS CONFIGURATION REGISTERS */
#define BCR_MSRDA    0
#define BCR_MSWRA    1
#define BCR_MC       2
#define BCR_LNKST    4
#define BCR_LED1     5
#define BCR_LED2     6
#define BCR_LED3     7
#define BCR_FDC      9
#define BCR_BSBC     18
#define BCR_EECAS    19
#define BCR_SWS      20
#define BCR_PLAT     22

#define BCR_DWIO(S)      !!((S)->bcr[BCR_BSBC] & 0x0080)
#define BCR_SSIZE32(S)   !!((S)->bcr[BCR_SWS ] & 0x0100)
#define BCR_SWSTYLE(S)     ((S)->bcr[BCR_SWS ] & 0x00FF)

#define CSR_INIT(S)      !!(((S)->csr[0])&0x0001)
#define CSR_STRT(S)      !!(((S)->csr[0])&0x0002)
#define CSR_STOP(S)      !!(((S)->csr[0])&0x0004)
#define CSR_TDMD(S)      !!(((S)->csr[0])&0x0008)
#define CSR_TXON(S)      !!(((S)->csr[0])&0x0010)
#define CSR_RXON(S)      !!(((S)->csr[0])&0x0020)
#define CSR_INEA(S)      !!(((S)->csr[0])&0x0040)
#define CSR_BSWP(S)      !!(((S)->csr[3])&0x0004)
#define CSR_LAPPEN(S)    !!(((S)->csr[3])&0x0020)
#define CSR_DXSUFLO(S)   !!(((S)->csr[3])&0x0040)
#define CSR_ASTRP_RCV(S) !!(((S)->csr[4])&0x0800)
#define CSR_DPOLL(S)     !!(((S)->csr[4])&0x1000)
#define CSR_SPND(S)      !!(((S)->csr[5])&0x0001)
#define CSR_LTINTEN(S)   !!(((S)->csr[5])&0x4000)
#define CSR_TOKINTD(S)   !!(((S)->csr[5])&0x8000)
#define CSR_DRX(S)       !!(((S)->csr[15])&0x0001)
#define CSR_DTX(S)       !!(((S)->csr[15])&0x0002)
#define CSR_LOOP(S)      !!(((S)->csr[15])&0x0004)
#define CSR_DXMTFCS(S)   !!(((S)->csr[15])&0x0008)
#define CSR_DRCVPA(S)    !!(((S)->csr[15])&0x2000)
#define CSR_DRCVBC(S)    !!(((S)->csr[15])&0x4000)
#define CSR_PROM(S)      !!(((S)->csr[15])&0x8000)

#define CSR_CRBC(S)      ((S)->csr[40])
#define CSR_CRST(S)      ((S)->csr[41])
#define CSR_CXBC(S)      ((S)->csr[42])
#define CSR_CXST(S)      ((S)->csr[43])
#define CSR_NRBC(S)      ((S)->csr[44])
#define CSR_NRST(S)      ((S)->csr[45])
#define CSR_POLL(S)      ((S)->csr[46])
#define CSR_PINT(S)      ((S)->csr[47])
#define CSR_RCVRC(S)     ((S)->csr[72])
#define CSR_XMTRC(S)     ((S)->csr[74])
#define CSR_RCVRL(S)     ((S)->csr[76])
#define CSR_XMTRL(S)     ((S)->csr[78])
#define CSR_MISSC(S)     ((S)->csr[112])

#define CSR_IADR(S)      ((S)->csr[ 1] | ((S)->csr[ 2] << 16))
#define CSR_CRBA(S)      ((S)->csr[18] | ((S)->csr[19] << 16))
#define CSR_CXBA(S)      ((S)->csr[20] | ((S)->csr[21] << 16))
#define CSR_NRBA(S)      ((S)->csr[22] | ((S)->csr[23] << 16))
#define CSR_BADR(S)      ((S)->csr[24] | ((S)->csr[25] << 16))
#define CSR_NRDA(S)      ((S)->csr[26] | ((S)->csr[27] << 16))
#define CSR_CRDA(S)      ((S)->csr[28] | ((S)->csr[29] << 16))
#define CSR_BADX(S)      ((S)->csr[30] | ((S)->csr[31] << 16))
#define CSR_NXDA(S)      ((S)->csr[32] | ((S)->csr[33] << 16))
#define CSR_CXDA(S)      ((S)->csr[34] | ((S)->csr[35] << 16))
#define CSR_NNRD(S)      ((S)->csr[36] | ((S)->csr[37] << 16))
#define CSR_NNXD(S)      ((S)->csr[38] | ((S)->csr[39] << 16))
#define CSR_PXDA(S)      ((S)->csr[60] | ((S)->csr[61] << 16))
#define CSR_NXBA(S)      ((S)->csr[64] | ((S)->csr[65] << 16))

#define PHYSADDR(S,A) \
  (BCR_SSIZE32(S) ? (A) : (A) | ((0xff00 & (uint32_t)(s)->csr[2])<<16))

struct pcnet_initblk16 {
    uint16_t mode;
    uint16_t padr[3];
    uint16_t ladrf[4];
    uint32_t rdra;
    uint32_t tdra;
};

struct pcnet_initblk32 {
    uint16_t mode;
    uint8_t rlen;
    uint8_t tlen;
    uint16_t padr[3];
    uint16_t _res;
    uint16_t ladrf[4];
    uint32_t rdra;
    uint32_t tdra;
};

struct pcnet_TMD {
    uint32_t tbadr;
    int16_t length;
    int16_t status;
    uint32_t misc;
    uint32_t res;
};

#define TMDL_BCNT_MASK  0x0fff
#define TMDL_BCNT_SH    0
#define TMDL_ONES_MASK  0xf000
#define TMDL_ONES_SH    12

#define TMDS_BPE_MASK   0x0080
#define TMDS_BPE_SH     7
#define TMDS_ENP_MASK   0x0100
#define TMDS_ENP_SH     8
#define TMDS_STP_MASK   0x0200
#define TMDS_STP_SH     9
#define TMDS_DEF_MASK   0x0400
#define TMDS_DEF_SH     10
#define TMDS_ONE_MASK   0x0800
#define TMDS_ONE_SH     11
#define TMDS_LTINT_MASK 0x1000
#define TMDS_LTINT_SH   12
#define TMDS_NOFCS_MASK 0x2000
#define TMDS_NOFCS_SH   13
#define TMDS_ADDFCS_MASK TMDS_NOFCS_MASK
#define TMDS_ADDFCS_SH  TMDS_NOFCS_SH
#define TMDS_ERR_MASK   0x4000
#define TMDS_ERR_SH     14
#define TMDS_OWN_MASK   0x8000
#define TMDS_OWN_SH     15

#define TMDM_TRC_MASK   0x0000000f
#define TMDM_TRC_SH     0
#define TMDM_TDR_MASK   0x03ff0000
#define TMDM_TDR_SH     16
#define TMDM_RTRY_MASK  0x04000000
#define TMDM_RTRY_SH    26
#define TMDM_LCAR_MASK  0x08000000
#define TMDM_LCAR_SH    27
#define TMDM_LCOL_MASK  0x10000000
#define TMDM_LCOL_SH    28
#define TMDM_EXDEF_MASK 0x20000000
#define TMDM_EXDEF_SH   29
#define TMDM_UFLO_MASK  0x40000000
#define TMDM_UFLO_SH    30
#define TMDM_BUFF_MASK  0x80000000
#define TMDM_BUFF_SH    31

struct pcnet_RMD {
    uint32_t rbadr;
    int16_t buf_length;
    int16_t status;
    uint32_t msg_length;
    uint32_t res;
};

#define RMDL_BCNT_MASK  0x0fff
#define RMDL_BCNT_SH    0
#define RMDL_ONES_MASK  0xf000
#define RMDL_ONES_SH    12

#define RMDS_BAM_MASK   0x0010
#define RMDS_BAM_SH     4
#define RMDS_LFAM_MASK  0x0020
#define RMDS_LFAM_SH    5
#define RMDS_PAM_MASK   0x0040
#define RMDS_PAM_SH     6
#define RMDS_BPE_MASK   0x0080
#define RMDS_BPE_SH     7
#define RMDS_ENP_MASK   0x0100
#define RMDS_ENP_SH     8
#define RMDS_STP_MASK   0x0200
#define RMDS_STP_SH     9
#define RMDS_BUFF_MASK  0x0400
#define RMDS_BUFF_SH    10
#define RMDS_CRC_MASK   0x0800
#define RMDS_CRC_SH     11
#define RMDS_OFLO_MASK  0x1000
#define RMDS_OFLO_SH    12
#define RMDS_FRAM_MASK  0x2000
#define RMDS_FRAM_SH    13
#define RMDS_ERR_MASK   0x4000
#define RMDS_ERR_SH     14
#define RMDS_OWN_MASK   0x8000
#define RMDS_OWN_SH     15

#define RMDM_MCNT_MASK  0x00000fff
#define RMDM_MCNT_SH    0
#define RMDM_ZEROS_MASK 0x0000f000
#define RMDM_ZEROS_SH   12
#define RMDM_RPC_MASK   0x00ff0000
#define RMDM_RPC_SH     16
#define RMDM_RCC_MASK   0xff000000
#define RMDM_RCC_SH     24

#define SET_FIELD(regp, name, field, value)             \
  (*(regp) = (*(regp) & ~(name ## _ ## field ## _MASK)) \
             | ((value) << name ## _ ## field ## _SH))

#define GET_FIELD(reg, name, field)                     \
  (((reg) & name ## _ ## field ## _MASK) >> name ## _ ## field ## _SH)

#define PRINT_TMD(T) printf(                            \
        "TMD0 : TBADR=0x%08x\n"                         \
        "TMD1 : OWN=%d, ERR=%d, FCS=%d, LTI=%d, "       \
        "ONE=%d, DEF=%d, STP=%d, ENP=%d,\n"             \
        "       BPE=%d, BCNT=%d\n"                      \
        "TMD2 : BUF=%d, UFL=%d, EXD=%d, LCO=%d, "       \
        "LCA=%d, RTR=%d,\n"                             \
        "       TDR=%d, TRC=%d\n",                      \
        (T)->tbadr,                                     \
        GET_FIELD((T)->status, TMDS, OWN),              \
        GET_FIELD((T)->status, TMDS, ERR),              \
        GET_FIELD((T)->status, TMDS, NOFCS),            \
        GET_FIELD((T)->status, TMDS, LTINT),            \
        GET_FIELD((T)->status, TMDS, ONE),              \
        GET_FIELD((T)->status, TMDS, DEF),              \
        GET_FIELD((T)->status, TMDS, STP),              \
        GET_FIELD((T)->status, TMDS, ENP),              \
        GET_FIELD((T)->status, TMDS, BPE),              \
        4096-GET_FIELD((T)->length, TMDL, BCNT),        \
        GET_FIELD((T)->misc, TMDM, BUFF),               \
        GET_FIELD((T)->misc, TMDM, UFLO),               \
        GET_FIELD((T)->misc, TMDM, EXDEF),              \
        GET_FIELD((T)->misc, TMDM, LCOL),               \
        GET_FIELD((T)->misc, TMDM, LCAR),               \
        GET_FIELD((T)->misc, TMDM, RTRY),               \
        GET_FIELD((T)->misc, TMDM, TDR),                \
        GET_FIELD((T)->misc, TMDM, TRC))

#define PRINT_RMD(R) printf(                            \
        "RMD0 : RBADR=0x%08x\n"                         \
        "RMD1 : OWN=%d, ERR=%d, FRAM=%d, OFLO=%d, "     \
        "CRC=%d, BUFF=%d, STP=%d, ENP=%d,\n       "     \
        "BPE=%d, PAM=%d, LAFM=%d, BAM=%d, ONES=%d, BCNT=%d\n" \
        "RMD2 : RCC=%d, RPC=%d, MCNT=%d, ZEROS=%d\n",   \
        (R)->rbadr,                                     \
        GET_FIELD((R)->status, RMDS, OWN),              \
        GET_FIELD((R)->status, RMDS, ERR),              \
        GET_FIELD((R)->status, RMDS, FRAM),             \
        GET_FIELD((R)->status, RMDS, OFLO),             \
        GET_FIELD((R)->status, RMDS, CRC),              \
        GET_FIELD((R)->status, RMDS, BUFF),             \
        GET_FIELD((R)->status, RMDS, STP),              \
        GET_FIELD((R)->status, RMDS, ENP),              \
        GET_FIELD((R)->status, RMDS, BPE),              \
        GET_FIELD((R)->status, RMDS, PAM),              \
        GET_FIELD((R)->status, RMDS, LFAM),             \
        GET_FIELD((R)->status, RMDS, BAM),              \
        GET_FIELD((R)->buf_length, RMDL, ONES),         \
        4096-GET_FIELD((R)->buf_length, RMDL, BCNT),    \
        GET_FIELD((R)->msg_length, RMDM, RCC),          \
        GET_FIELD((R)->msg_length, RMDM, RPC),          \
        GET_FIELD((R)->msg_length, RMDM, MCNT),         \
        GET_FIELD((R)->msg_length, RMDM, ZEROS))

static inline void pcnet_tmd_load(PCNetState *s, struct pcnet_TMD *tmd,
                                  target_phys_addr_t addr)
{
    if (!BCR_SSIZE32(s)) {
        struct {
            uint32_t tbadr;
            int16_t length;
            int16_t status;
	} xda;
        s->phys_mem_read(s->dma_opaque, addr, (void *)&xda, sizeof(xda), 0);
        tmd->tbadr = le32_to_cpu(xda.tbadr) & 0xffffff;
        tmd->length = le16_to_cpu(xda.length);
        tmd->status = (le32_to_cpu(xda.tbadr) >> 16) & 0xff00;
        tmd->misc = le16_to_cpu(xda.status) << 16;
        tmd->res = 0;
    } else {
        s->phys_mem_read(s->dma_opaque, addr, (void *)tmd, sizeof(*tmd), 0);
        le32_to_cpus(&tmd->tbadr);
        le16_to_cpus((uint16_t *)&tmd->length);
        le16_to_cpus((uint16_t *)&tmd->status);
        le32_to_cpus(&tmd->misc);
        le32_to_cpus(&tmd->res);
        if (BCR_SWSTYLE(s) == 3) {
            uint32_t tmp = tmd->tbadr;
            tmd->tbadr = tmd->misc;
            tmd->misc = tmp;
        }
    }
}

static inline void pcnet_tmd_store(PCNetState *s, const struct pcnet_TMD *tmd,
                                   target_phys_addr_t addr)
{
    if (!BCR_SSIZE32(s)) {
        struct {
            uint32_t tbadr;
            int16_t length;
            int16_t status;
        } xda;
        xda.tbadr = cpu_to_le32((tmd->tbadr & 0xffffff) |
                                ((tmd->status & 0xff00) << 16));
        xda.length = cpu_to_le16(tmd->length);
        xda.status = cpu_to_le16(tmd->misc >> 16);
        s->phys_mem_write(s->dma_opaque, addr, (void *)&xda, sizeof(xda), 0);
    } else {
        struct {
            uint32_t tbadr;
            int16_t length;
            int16_t status;
            uint32_t misc;
            uint32_t res;
        } xda;
        xda.tbadr = cpu_to_le32(tmd->tbadr);
        xda.length = cpu_to_le16(tmd->length);
        xda.status = cpu_to_le16(tmd->status);
        xda.misc = cpu_to_le32(tmd->misc);
        xda.res = cpu_to_le32(tmd->res);
        if (BCR_SWSTYLE(s) == 3) {
            uint32_t tmp = xda.tbadr;
            xda.tbadr = xda.misc;
            xda.misc = tmp;
        }
        s->phys_mem_write(s->dma_opaque, addr, (void *)&xda, sizeof(xda), 0);
    }
}

static inline void pcnet_rmd_load(PCNetState *s, struct pcnet_RMD *rmd,
                                  target_phys_addr_t addr)
{
    if (!BCR_SSIZE32(s)) {
        struct {
            uint32_t rbadr;
            int16_t buf_length;
            int16_t msg_length;
	} rda;
        s->phys_mem_read(s->dma_opaque, addr, (void *)&rda, sizeof(rda), 0);
        rmd->rbadr = le32_to_cpu(rda.rbadr) & 0xffffff;
        rmd->buf_length = le16_to_cpu(rda.buf_length);
        rmd->status = (le32_to_cpu(rda.rbadr) >> 16) & 0xff00;
        rmd->msg_length = le16_to_cpu(rda.msg_length);
        rmd->res = 0;
    } else {
        s->phys_mem_read(s->dma_opaque, addr, (void *)rmd, sizeof(*rmd), 0);
        le32_to_cpus(&rmd->rbadr);
        le16_to_cpus((uint16_t *)&rmd->buf_length);
        le16_to_cpus((uint16_t *)&rmd->status);
        le32_to_cpus(&rmd->msg_length);
        le32_to_cpus(&rmd->res);
        if (BCR_SWSTYLE(s) == 3) {
            uint32_t tmp = rmd->rbadr;
            rmd->rbadr = rmd->msg_length;
            rmd->msg_length = tmp;
        }
    }
}

static inline void pcnet_rmd_store(PCNetState *s, struct pcnet_RMD *rmd,
                                   target_phys_addr_t addr)
{
    if (!BCR_SSIZE32(s)) {
        struct {
            uint32_t rbadr;
            int16_t buf_length;
            int16_t msg_length;
        } rda;
        rda.rbadr = cpu_to_le32((rmd->rbadr & 0xffffff) |
                                ((rmd->status & 0xff00) << 16));
        rda.buf_length = cpu_to_le16(rmd->buf_length);
        rda.msg_length = cpu_to_le16(rmd->msg_length);
        s->phys_mem_write(s->dma_opaque, addr, (void *)&rda, sizeof(rda), 0);
    } else {
        struct {
            uint32_t rbadr;
            int16_t buf_length;
            int16_t status;
            uint32_t msg_length;
            uint32_t res;
        } rda;
        rda.rbadr = cpu_to_le32(rmd->rbadr);
        rda.buf_length = cpu_to_le16(rmd->buf_length);
        rda.status = cpu_to_le16(rmd->status);
        rda.msg_length = cpu_to_le32(rmd->msg_length);
        rda.res = cpu_to_le32(rmd->res);
        if (BCR_SWSTYLE(s) == 3) {
            uint32_t tmp = rda.rbadr;
            rda.rbadr = rda.msg_length;
            rda.msg_length = tmp;
        }
        s->phys_mem_write(s->dma_opaque, addr, (void *)&rda, sizeof(rda), 0);
    }
}


#define TMDLOAD(TMD,ADDR) pcnet_tmd_load(s,TMD,ADDR)

#define TMDSTORE(TMD,ADDR) pcnet_tmd_store(s,TMD,ADDR)

#define RMDLOAD(RMD,ADDR) pcnet_rmd_load(s,RMD,ADDR)

#define RMDSTORE(RMD,ADDR) pcnet_rmd_store(s,RMD,ADDR)

#if 1

#define CHECK_RMD(ADDR,RES) do {                \
    struct pcnet_RMD rmd;                       \
    RMDLOAD(&rmd,(ADDR));                       \
    (RES) |= (GET_FIELD(rmd.buf_length, RMDL, ONES) != 15) \
          || (GET_FIELD(rmd.msg_length, RMDM, ZEROS) != 0); \
} while (0)

#define CHECK_TMD(ADDR,RES) do {                \
    struct pcnet_TMD tmd;                       \
    TMDLOAD(&tmd,(ADDR));                       \
    (RES) |= (GET_FIELD(tmd.length, TMDL, ONES) != 15); \
} while (0)

#else

#define CHECK_RMD(ADDR,RES) do {                \
    switch (BCR_SWSTYLE(s)) {                   \
    case 0x00:                                  \
        do {                                    \
            uint16_t rda[4];                    \
            s->phys_mem_read(s->dma_opaque, (ADDR), \
                (void *)&rda[0], sizeof(rda), 0); \
            (RES) |= (rda[2] & 0xf000)!=0xf000; \
            (RES) |= (rda[3] & 0xf000)!=0x0000; \
        } while (0);                            \
        break;                                  \
    case 0x01:                                  \
    case 0x02:                                  \
        do {                                    \
            uint32_t rda[4];                    \
            s->phys_mem_read(s->dma_opaque, (ADDR), \
                (void *)&rda[0], sizeof(rda), 0); \
            (RES) |= (rda[1] & 0x0000f000L)!=0x0000f000L; \
            (RES) |= (rda[2] & 0x0000f000L)!=0x00000000L; \
        } while (0);                            \
        break;                                  \
    case 0x03:                                  \
        do {                                    \
            uint32_t rda[4];                    \
            s->phys_mem_read(s->dma_opaque, (ADDR), \
                (void *)&rda[0], sizeof(rda), 0); \
            (RES) |= (rda[0] & 0x0000f000L)!=0x00000000L; \
            (RES) |= (rda[1] & 0x0000f000L)!=0x0000f000L; \
        } while (0);                            \
        break;                                  \
    }                                           \
} while (0)

#define CHECK_TMD(ADDR,RES) do {                \
    switch (BCR_SWSTYLE(s)) {                   \
    case 0x00:                                  \
        do {                                    \
            uint16_t xda[4];                    \
            s->phys_mem_read(s->dma_opaque, (ADDR), \
                (void *)&xda[0], sizeof(xda), 0); \
            (RES) |= (xda[2] & 0xf000)!=0xf000; \
        } while (0);                            \
        break;                                  \
    case 0x01:                                  \
    case 0x02:                                  \
    case 0x03:                                  \
        do {                                    \
            uint32_t xda[4];                    \
            s->phys_mem_read(s->dma_opaque, (ADDR), \
                (void *)&xda[0], sizeof(xda), 0); \
            (RES) |= (xda[1] & 0x0000f000L)!=0x0000f000L; \
        } while (0);                            \
        break;                                  \
    }                                           \
} while (0)

#endif

#define PRINT_PKTHDR(BUF) do {                  \
    struct qemu_ether_header *hdr = (void *)(BUF); \
    printf("packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, " \
           "shost=%02x:%02x:%02x:%02x:%02x:%02x, " \
           "type=0x%04x\n",                     \
           hdr->ether_dhost[0],hdr->ether_dhost[1],hdr->ether_dhost[2], \
           hdr->ether_dhost[3],hdr->ether_dhost[4],hdr->ether_dhost[5], \
           hdr->ether_shost[0],hdr->ether_shost[1],hdr->ether_shost[2], \
           hdr->ether_shost[3],hdr->ether_shost[4],hdr->ether_shost[5], \
           be16_to_cpu(hdr->ether_type));       \
} while (0)

#define MULTICAST_FILTER_LEN 8

static inline uint32_t lnc_mchash(const uint8_t *ether_addr)
{
#define LNC_POLYNOMIAL          0xEDB88320UL
    uint32_t crc = 0xFFFFFFFF;
    int idx, bit;
    uint8_t data;

    for (idx = 0; idx < 6; idx++) {
        for (data = *ether_addr++, bit = 0; bit < MULTICAST_FILTER_LEN; bit++) {
            crc = (crc >> 1) ^ (((crc ^ data) & 1) ? LNC_POLYNOMIAL : 0);
            data >>= 1;
        }
    }
    return crc;
#undef LNC_POLYNOMIAL
}

#define CRC(crc, ch)	 (crc = (crc >> 8) ^ crctab[(crc ^ (ch)) & 0xff])

/* generated using the AUTODIN II polynomial
 *	x^32 + x^26 + x^23 + x^22 + x^16 +
 *	x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1
 */
static const uint32_t crctab[256] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
	0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
	0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
	0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
	0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
	0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
	0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
	0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
	0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
	0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
	0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
	0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
	0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
	0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
	0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
	0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
	0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
	0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
	0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
	0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
	0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
	0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
	0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
	0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
	0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
	0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
	0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
	0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
	0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
	0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
	0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
	0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
	0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
	0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
	0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
	0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
	0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
	0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
	0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

static inline int padr_match(PCNetState *s, const uint8_t *buf, int size)
{
    struct qemu_ether_header *hdr = (void *)buf;
    uint8_t padr[6] = {
        s->csr[12] & 0xff, s->csr[12] >> 8,
        s->csr[13] & 0xff, s->csr[13] >> 8,
        s->csr[14] & 0xff, s->csr[14] >> 8
    };
    int result = (!CSR_DRCVPA(s)) && !memcmp(hdr->ether_dhost, padr, 6);
#ifdef PCNET_DEBUG_MATCH
    printf("packet dhost=%02x:%02x:%02x:%02x:%02x:%02x, "
           "padr=%02x:%02x:%02x:%02x:%02x:%02x\n",
           hdr->ether_dhost[0],hdr->ether_dhost[1],hdr->ether_dhost[2],
           hdr->ether_dhost[3],hdr->ether_dhost[4],hdr->ether_dhost[5],
           padr[0],padr[1],padr[2],padr[3],padr[4],padr[5]);
    printf("padr_match result=%d\n", result);
#endif
    return result;
}

static inline int padr_bcast(PCNetState *s, const uint8_t *buf, int size)
{
    static const uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    struct qemu_ether_header *hdr = (void *)buf;
    int result = !CSR_DRCVBC(s) && !memcmp(hdr->ether_dhost, BCAST, 6);
#ifdef PCNET_DEBUG_MATCH
    printf("padr_bcast result=%d\n", result);
#endif
    return result;
}

static inline int ladr_match(PCNetState *s, const uint8_t *buf, int size)
{
    struct qemu_ether_header *hdr = (void *)buf;
    if ((*(hdr->ether_dhost)&0x01) &&
        ((uint64_t *)&s->csr[8])[0] != 0LL) {
        uint8_t ladr[8] = {
            s->csr[8] & 0xff, s->csr[8] >> 8,
            s->csr[9] & 0xff, s->csr[9] >> 8,
            s->csr[10] & 0xff, s->csr[10] >> 8,
            s->csr[11] & 0xff, s->csr[11] >> 8
        };
        int index = lnc_mchash(hdr->ether_dhost) >> 26;
        return !!(ladr[index >> 3] & (1 << (index & 7)));
    }
    return 0;
}

static inline target_phys_addr_t pcnet_rdra_addr(PCNetState *s, int idx)
{
    while (idx < 1) idx += CSR_RCVRL(s);
    return s->rdra + ((CSR_RCVRL(s) - idx) * (BCR_SWSTYLE(s) ? 16 : 8));
}

static inline int64_t pcnet_get_next_poll_time(PCNetState *s, int64_t current_time)
{
    int64_t next_time = current_time +
        muldiv64(65536 - (CSR_SPND(s) ? 0 : CSR_POLL(s)),
                 ticks_per_sec, 33000000L);
    if (next_time <= current_time)
        next_time = current_time + 1;
    return next_time;
}

static void pcnet_poll(PCNetState *s);
static void pcnet_poll_timer(void *opaque);

static uint32_t pcnet_csr_readw(PCNetState *s, uint32_t rap);
static void pcnet_csr_writew(PCNetState *s, uint32_t rap, uint32_t new_value);
static void pcnet_bcr_writew(PCNetState *s, uint32_t rap, uint32_t val);
static uint32_t pcnet_bcr_readw(PCNetState *s, uint32_t rap);

static void pcnet_s_reset(PCNetState *s)
{
#ifdef PCNET_DEBUG
    printf("pcnet_s_reset\n");
#endif

    s->lnkst = 0x40;
    s->rdra = 0;
    s->tdra = 0;
    s->rap = 0;

    s->bcr[BCR_BSBC] &= ~0x0080;

    s->csr[0]   = 0x0004;
    s->csr[3]   = 0x0000;
    s->csr[4]   = 0x0115;
    s->csr[5]   = 0x0000;
    s->csr[6]   = 0x0000;
    s->csr[8]   = 0;
    s->csr[9]   = 0;
    s->csr[10]  = 0;
    s->csr[11]  = 0;
    s->csr[12]  = le16_to_cpu(((uint16_t *)&s->prom[0])[0]);
    s->csr[13]  = le16_to_cpu(((uint16_t *)&s->prom[0])[1]);
    s->csr[14]  = le16_to_cpu(((uint16_t *)&s->prom[0])[2]);
    s->csr[15] &= 0x21c4;
    s->csr[72]  = 1;
    s->csr[74]  = 1;
    s->csr[76]  = 1;
    s->csr[78]  = 1;
    s->csr[80]  = 0x1410;
    s->csr[88]  = 0x1003;
    s->csr[89]  = 0x0262;
    s->csr[94]  = 0x0000;
    s->csr[100] = 0x0200;
    s->csr[103] = 0x0105;
    s->csr[103] = 0x0105;
    s->csr[112] = 0x0000;
    s->csr[114] = 0x0000;
    s->csr[122] = 0x0000;
    s->csr[124] = 0x0000;

    s->tx_busy = 0;
}

static void pcnet_update_irq(PCNetState *s)
{
    int isr = 0;
    s->csr[0] &= ~0x0080;

#if 1
    if (((s->csr[0] & ~s->csr[3]) & 0x5f00) ||
        (((s->csr[4]>>1) & ~s->csr[4]) & 0x0115) ||
        (((s->csr[5]>>1) & s->csr[5]) & 0x0048))
#else
    if ((!(s->csr[3] & 0x4000) && !!(s->csr[0] & 0x4000)) /* BABL */ ||
        (!(s->csr[3] & 0x1000) && !!(s->csr[0] & 0x1000)) /* MISS */ ||
        (!(s->csr[3] & 0x0100) && !!(s->csr[0] & 0x0100)) /* IDON */ ||
        (!(s->csr[3] & 0x0200) && !!(s->csr[0] & 0x0200)) /* TINT */ ||
        (!(s->csr[3] & 0x0400) && !!(s->csr[0] & 0x0400)) /* RINT */ ||
        (!(s->csr[3] & 0x0800) && !!(s->csr[0] & 0x0800)) /* MERR */ ||
        (!(s->csr[4] & 0x0001) && !!(s->csr[4] & 0x0002)) /* JAB */ ||
        (!(s->csr[4] & 0x0004) && !!(s->csr[4] & 0x0008)) /* TXSTRT */ ||
        (!(s->csr[4] & 0x0010) && !!(s->csr[4] & 0x0020)) /* RCVO */ ||
        (!(s->csr[4] & 0x0100) && !!(s->csr[4] & 0x0200)) /* MFCO */ ||
        (!!(s->csr[5] & 0x0040) && !!(s->csr[5] & 0x0080)) /* EXDINT */ ||
        (!!(s->csr[5] & 0x0008) && !!(s->csr[5] & 0x0010)) /* MPINT */)
#endif
    {

        isr = CSR_INEA(s);
        s->csr[0] |= 0x0080;
    }

    if (!!(s->csr[4] & 0x0080) && CSR_INEA(s)) { /* UINT */
        s->csr[4] &= ~0x0080;
        s->csr[4] |= 0x0040;
        s->csr[0] |= 0x0080;
        isr = 1;
#ifdef PCNET_DEBUG
        printf("pcnet user int\n");
#endif
    }

#if 1
    if (((s->csr[5]>>1) & s->csr[5]) & 0x0500)
#else
    if ((!!(s->csr[5] & 0x0400) && !!(s->csr[5] & 0x0800)) /* SINT */ ||
        (!!(s->csr[5] & 0x0100) && !!(s->csr[5] & 0x0200)) /* SLPINT */ )
#endif
    {
        isr = 1;
        s->csr[0] |= 0x0080;
    }

    if (isr != s->isr) {
#ifdef PCNET_DEBUG
        printf("pcnet: INTA=%d\n", isr);
#endif
    }
    qemu_set_irq(s->irq, isr);
    s->isr = isr;
}

static void pcnet_init(PCNetState *s)
{
    int rlen, tlen;
    uint16_t padr[3], ladrf[4], mode;
    uint32_t rdra, tdra;

#ifdef PCNET_DEBUG
    printf("pcnet_init init_addr=0x%08x\n", PHYSADDR(s,CSR_IADR(s)));
#endif

    if (BCR_SSIZE32(s)) {
        struct pcnet_initblk32 initblk;
        s->phys_mem_read(s->dma_opaque, PHYSADDR(s,CSR_IADR(s)),
                (uint8_t *)&initblk, sizeof(initblk), 0);
        mode = le16_to_cpu(initblk.mode);
        rlen = initblk.rlen >> 4;
        tlen = initblk.tlen >> 4;
	ladrf[0] = le16_to_cpu(initblk.ladrf[0]);
	ladrf[1] = le16_to_cpu(initblk.ladrf[1]);
	ladrf[2] = le16_to_cpu(initblk.ladrf[2]);
	ladrf[3] = le16_to_cpu(initblk.ladrf[3]);
	padr[0] = le16_to_cpu(initblk.padr[0]);
	padr[1] = le16_to_cpu(initblk.padr[1]);
	padr[2] = le16_to_cpu(initblk.padr[2]);
        rdra = le32_to_cpu(initblk.rdra);
        tdra = le32_to_cpu(initblk.tdra);
    } else {
        struct pcnet_initblk16 initblk;
        s->phys_mem_read(s->dma_opaque, PHYSADDR(s,CSR_IADR(s)),
                (uint8_t *)&initblk, sizeof(initblk), 0);
        mode = le16_to_cpu(initblk.mode);
	ladrf[0] = le16_to_cpu(initblk.ladrf[0]);
	ladrf[1] = le16_to_cpu(initblk.ladrf[1]);
	ladrf[2] = le16_to_cpu(initblk.ladrf[2]);
	ladrf[3] = le16_to_cpu(initblk.ladrf[3]);
	padr[0] = le16_to_cpu(initblk.padr[0]);
	padr[1] = le16_to_cpu(initblk.padr[1]);
	padr[2] = le16_to_cpu(initblk.padr[2]);
        rdra = le32_to_cpu(initblk.rdra);
        tdra = le32_to_cpu(initblk.tdra);
        rlen = rdra >> 29;
        tlen = tdra >> 29;
        rdra &= 0x00ffffff;
        tdra &= 0x00ffffff;
    }

#if defined(PCNET_DEBUG)
    printf("rlen=%d tlen=%d\n", rlen, tlen);
#endif

    CSR_RCVRL(s) = (rlen < 9) ? (1 << rlen) : 512;
    CSR_XMTRL(s) = (tlen < 9) ? (1 << tlen) : 512;
    s->csr[ 6] = (tlen << 12) | (rlen << 8);
    s->csr[15] = mode;
    s->csr[ 8] = ladrf[0];
    s->csr[ 9] = ladrf[1];
    s->csr[10] = ladrf[2];
    s->csr[11] = ladrf[3];
    s->csr[12] = padr[0];
    s->csr[13] = padr[1];
    s->csr[14] = padr[2];
    s->rdra = PHYSADDR(s, rdra);
    s->tdra = PHYSADDR(s, tdra);

    CSR_RCVRC(s) = CSR_RCVRL(s);
    CSR_XMTRC(s) = CSR_XMTRL(s);

#ifdef PCNET_DEBUG
    printf("pcnet ss32=%d rdra=0x%08x[%d] tdra=0x%08x[%d]\n",
        BCR_SSIZE32(s),
        s->rdra, CSR_RCVRL(s), s->tdra, CSR_XMTRL(s));
#endif

    s->csr[0] |= 0x0101;
    s->csr[0] &= ~0x0004;       /* clear STOP bit */
}

static void pcnet_start(PCNetState *s)
{
#ifdef PCNET_DEBUG
    printf("pcnet_start\n");
#endif

    if (!CSR_DTX(s))
        s->csr[0] |= 0x0010;    /* set TXON */

    if (!CSR_DRX(s))
        s->csr[0] |= 0x0020;    /* set RXON */

    s->csr[0] &= ~0x0004;       /* clear STOP bit */
    s->csr[0] |= 0x0002;
}

static void pcnet_stop(PCNetState *s)
{
#ifdef PCNET_DEBUG
    printf("pcnet_stop\n");
#endif
    s->csr[0] &= ~0x7feb;
    s->csr[0] |= 0x0014;
    s->csr[4] &= ~0x02c2;
    s->csr[5] &= ~0x0011;
    pcnet_poll_timer(s);
}

static void pcnet_rdte_poll(PCNetState *s)
{
    s->csr[28] = s->csr[29] = 0;
    if (s->rdra) {
        int bad = 0;
#if 1
        target_phys_addr_t crda = pcnet_rdra_addr(s, CSR_RCVRC(s));
        target_phys_addr_t nrda = pcnet_rdra_addr(s, -1 + CSR_RCVRC(s));
        target_phys_addr_t nnrd = pcnet_rdra_addr(s, -2 + CSR_RCVRC(s));
#else
        target_phys_addr_t crda = s->rdra +
            (CSR_RCVRL(s) - CSR_RCVRC(s)) *
            (BCR_SWSTYLE(s) ? 16 : 8 );
        int nrdc = CSR_RCVRC(s)<=1 ? CSR_RCVRL(s) : CSR_RCVRC(s)-1;
        target_phys_addr_t nrda = s->rdra +
            (CSR_RCVRL(s) - nrdc) *
            (BCR_SWSTYLE(s) ? 16 : 8 );
        int nnrc = nrdc<=1 ? CSR_RCVRL(s) : nrdc-1;
        target_phys_addr_t nnrd = s->rdra +
            (CSR_RCVRL(s) - nnrc) *
            (BCR_SWSTYLE(s) ? 16 : 8 );
#endif

        CHECK_RMD(crda, bad);
        if (!bad) {
            CHECK_RMD(nrda, bad);
            if (bad || (nrda == crda)) nrda = 0;
            CHECK_RMD(nnrd, bad);
            if (bad || (nnrd == crda)) nnrd = 0;

            s->csr[28] = crda & 0xffff;
            s->csr[29] = crda >> 16;
            s->csr[26] = nrda & 0xffff;
            s->csr[27] = nrda >> 16;
            s->csr[36] = nnrd & 0xffff;
            s->csr[37] = nnrd >> 16;
#ifdef PCNET_DEBUG
            if (bad) {
                printf("pcnet: BAD RMD RECORDS AFTER 0x" TARGET_FMT_plx "\n",
                       crda);
            }
        } else {
            printf("pcnet: BAD RMD RDA=0x" TARGET_FMT_plx "\n",
                   crda);
#endif
        }
    }

    if (CSR_CRDA(s)) {
        struct pcnet_RMD rmd;
        RMDLOAD(&rmd, PHYSADDR(s,CSR_CRDA(s)));
        CSR_CRBC(s) = GET_FIELD(rmd.buf_length, RMDL, BCNT);
        CSR_CRST(s) = rmd.status;
#ifdef PCNET_DEBUG_RMD_X
        printf("CRDA=0x%08x CRST=0x%04x RCVRC=%d RMDL=0x%04x RMDS=0x%04x RMDM=0x%08x\n",
                PHYSADDR(s,CSR_CRDA(s)), CSR_CRST(s), CSR_RCVRC(s),
                rmd.buf_length, rmd.status, rmd.msg_length);
        PRINT_RMD(&rmd);
#endif
    } else {
        CSR_CRBC(s) = CSR_CRST(s) = 0;
    }

    if (CSR_NRDA(s)) {
        struct pcnet_RMD rmd;
        RMDLOAD(&rmd, PHYSADDR(s,CSR_NRDA(s)));
        CSR_NRBC(s) = GET_FIELD(rmd.buf_length, RMDL, BCNT);
        CSR_NRST(s) = rmd.status;
    } else {
        CSR_NRBC(s) = CSR_NRST(s) = 0;
    }

}

static int pcnet_tdte_poll(PCNetState *s)
{
    s->csr[34] = s->csr[35] = 0;
    if (s->tdra) {
        target_phys_addr_t cxda = s->tdra +
            (CSR_XMTRL(s) - CSR_XMTRC(s)) *
            (BCR_SWSTYLE(s) ? 16 : 8);
        int bad = 0;
        CHECK_TMD(cxda, bad);
        if (!bad) {
            if (CSR_CXDA(s) != cxda) {
                s->csr[60] = s->csr[34];
                s->csr[61] = s->csr[35];
                s->csr[62] = CSR_CXBC(s);
                s->csr[63] = CSR_CXST(s);
            }
            s->csr[34] = cxda & 0xffff;
            s->csr[35] = cxda >> 16;
#ifdef PCNET_DEBUG_X
            printf("pcnet: BAD TMD XDA=0x%08x\n", cxda);
#endif
        }
    }

    if (CSR_CXDA(s)) {
        struct pcnet_TMD tmd;

        TMDLOAD(&tmd, PHYSADDR(s,CSR_CXDA(s)));

        CSR_CXBC(s) = GET_FIELD(tmd.length, TMDL, BCNT);
        CSR_CXST(s) = tmd.status;
    } else {
        CSR_CXBC(s) = CSR_CXST(s) = 0;
    }

    return !!(CSR_CXST(s) & 0x8000);
}

static int pcnet_can_receive(void *opaque)
{
    PCNetState *s = opaque;
    if (CSR_STOP(s) || CSR_SPND(s))
        return 0;

    if (s->recv_pos > 0)
        return 0;

    return sizeof(s->buffer)-16;
}

#define MIN_BUF_SIZE 60

static void pcnet_receive(void *opaque, const uint8_t *buf, int size)
{
    PCNetState *s = opaque;
    int is_padr = 0, is_bcast = 0, is_ladr = 0;
    uint8_t buf1[60];
    int remaining;
    int crc_err = 0;

    if (CSR_DRX(s) || CSR_STOP(s) || CSR_SPND(s) || !size)
        return;

#ifdef PCNET_DEBUG
    printf("pcnet_receive size=%d\n", size);
#endif

    /* if too small buffer, then expand it */
    if (size < MIN_BUF_SIZE) {
        memcpy(buf1, buf, size);
        memset(buf1 + size, 0, MIN_BUF_SIZE - size);
        buf = buf1;
        size = MIN_BUF_SIZE;
    }

    if (CSR_PROM(s)
        || (is_padr=padr_match(s, buf, size))
        || (is_bcast=padr_bcast(s, buf, size))
        || (is_ladr=ladr_match(s, buf, size))) {

        pcnet_rdte_poll(s);

        if (!(CSR_CRST(s) & 0x8000) && s->rdra) {
            struct pcnet_RMD rmd;
            int rcvrc = CSR_RCVRC(s)-1,i;
            target_phys_addr_t nrda;
            for (i = CSR_RCVRL(s)-1; i > 0; i--, rcvrc--) {
                if (rcvrc <= 1)
                    rcvrc = CSR_RCVRL(s);
                nrda = s->rdra +
                    (CSR_RCVRL(s) - rcvrc) *
                    (BCR_SWSTYLE(s) ? 16 : 8 );
                RMDLOAD(&rmd, nrda);
                if (GET_FIELD(rmd.status, RMDS, OWN)) {
#ifdef PCNET_DEBUG_RMD
                    printf("pcnet - scan buffer: RCVRC=%d PREV_RCVRC=%d\n",
                                rcvrc, CSR_RCVRC(s));
#endif
                    CSR_RCVRC(s) = rcvrc;
                    pcnet_rdte_poll(s);
                    break;
                }
            }
        }

        if (!(CSR_CRST(s) & 0x8000)) {
#ifdef PCNET_DEBUG_RMD
            printf("pcnet - no buffer: RCVRC=%d\n", CSR_RCVRC(s));
#endif
            s->csr[0] |= 0x1000; /* Set MISS flag */
            CSR_MISSC(s)++;
        } else {
            uint8_t *src = s->buffer;
            target_phys_addr_t crda = CSR_CRDA(s);
            struct pcnet_RMD rmd;
            int pktcount = 0;

            if (!s->looptest) {
                memcpy(src, buf, size);
                /* no need to compute the CRC */
                src[size] = 0;
                src[size + 1] = 0;
                src[size + 2] = 0;
                src[size + 3] = 0;
                size += 4;
            } else if (s->looptest == PCNET_LOOPTEST_CRC ||
                       !CSR_DXMTFCS(s) || size < MIN_BUF_SIZE+4) {
                uint32_t fcs = ~0;
                uint8_t *p = src;

                while (p != &src[size])
                    CRC(fcs, *p++);
                *(uint32_t *)p = htonl(fcs);
                size += 4;
            } else {
                uint32_t fcs = ~0;
                uint8_t *p = src;

                while (p != &src[size-4])
                    CRC(fcs, *p++);
                crc_err = (*(uint32_t *)p != htonl(fcs));
            }

#ifdef PCNET_DEBUG_MATCH
            PRINT_PKTHDR(buf);
#endif

            RMDLOAD(&rmd, PHYSADDR(s,crda));
            /*if (!CSR_LAPPEN(s))*/
                SET_FIELD(&rmd.status, RMDS, STP, 1);

#define PCNET_RECV_STORE() do {                                 \
    int count = MIN(4096 - GET_FIELD(rmd.buf_length, RMDL, BCNT),remaining); \
    target_phys_addr_t rbadr = PHYSADDR(s, rmd.rbadr);          \
    s->phys_mem_write(s->dma_opaque, rbadr, src, count, CSR_BSWP(s)); \
    src += count; remaining -= count;                           \
    SET_FIELD(&rmd.status, RMDS, OWN, 0);                       \
    RMDSTORE(&rmd, PHYSADDR(s,crda));                           \
    pktcount++;                                                 \
} while (0)

            remaining = size;
            PCNET_RECV_STORE();
            if ((remaining > 0) && CSR_NRDA(s)) {
                target_phys_addr_t nrda = CSR_NRDA(s);
#ifdef PCNET_DEBUG_RMD
                PRINT_RMD(&rmd);
#endif
                RMDLOAD(&rmd, PHYSADDR(s,nrda));
                if (GET_FIELD(rmd.status, RMDS, OWN)) {
                    crda = nrda;
                    PCNET_RECV_STORE();
#ifdef PCNET_DEBUG_RMD
                    PRINT_RMD(&rmd);
#endif
                    if ((remaining > 0) && (nrda=CSR_NNRD(s))) {
                        RMDLOAD(&rmd, PHYSADDR(s,nrda));
                        if (GET_FIELD(rmd.status, RMDS, OWN)) {
                            crda = nrda;
                            PCNET_RECV_STORE();
                        }
                    }
                }
            }

#undef PCNET_RECV_STORE

            RMDLOAD(&rmd, PHYSADDR(s,crda));
            if (remaining == 0) {
                SET_FIELD(&rmd.msg_length, RMDM, MCNT, size);
                SET_FIELD(&rmd.status, RMDS, ENP, 1);
                SET_FIELD(&rmd.status, RMDS, PAM, !CSR_PROM(s) && is_padr);
                SET_FIELD(&rmd.status, RMDS, LFAM, !CSR_PROM(s) && is_ladr);
                SET_FIELD(&rmd.status, RMDS, BAM, !CSR_PROM(s) && is_bcast);
                if (crc_err) {
                    SET_FIELD(&rmd.status, RMDS, CRC, 1);
                    SET_FIELD(&rmd.status, RMDS, ERR, 1);
                }
            } else {
                SET_FIELD(&rmd.status, RMDS, OFLO, 1);
                SET_FIELD(&rmd.status, RMDS, BUFF, 1);
                SET_FIELD(&rmd.status, RMDS, ERR, 1);
            }
            RMDSTORE(&rmd, PHYSADDR(s,crda));
            s->csr[0] |= 0x0400;

#ifdef PCNET_DEBUG
            printf("RCVRC=%d CRDA=0x%08x BLKS=%d\n",
                CSR_RCVRC(s), PHYSADDR(s,CSR_CRDA(s)), pktcount);
#endif
#ifdef PCNET_DEBUG_RMD
            PRINT_RMD(&rmd);
#endif

            while (pktcount--) {
                if (CSR_RCVRC(s) <= 1)
                    CSR_RCVRC(s) = CSR_RCVRL(s);
                else
                    CSR_RCVRC(s)--;
            }

            pcnet_rdte_poll(s);

        }
    }

    pcnet_poll(s);
    pcnet_update_irq(s);
}

static void pcnet_transmit(PCNetState *s)
{
    target_phys_addr_t xmit_cxda = 0;
    int count = CSR_XMTRL(s)-1;
    int add_crc = 0;

    s->xmit_pos = -1;

    if (!CSR_TXON(s)) {
        s->csr[0] &= ~0x0008;
        return;
    }

    s->tx_busy = 1;

    txagain:
    if (pcnet_tdte_poll(s)) {
        struct pcnet_TMD tmd;

        TMDLOAD(&tmd, PHYSADDR(s,CSR_CXDA(s)));

#ifdef PCNET_DEBUG_TMD
        printf("  TMDLOAD 0x%08x\n", PHYSADDR(s,CSR_CXDA(s)));
        PRINT_TMD(&tmd);
#endif
        if (GET_FIELD(tmd.status, TMDS, STP)) {
            s->xmit_pos = 0;
            xmit_cxda = PHYSADDR(s,CSR_CXDA(s));
            if (BCR_SWSTYLE(s) != 1)
                add_crc = GET_FIELD(tmd.status, TMDS, ADDFCS);
        }
        if (!GET_FIELD(tmd.status, TMDS, ENP)) {
            int bcnt = 4096 - GET_FIELD(tmd.length, TMDL, BCNT);
            s->phys_mem_read(s->dma_opaque, PHYSADDR(s, tmd.tbadr),
                             s->buffer + s->xmit_pos, bcnt, CSR_BSWP(s));
            s->xmit_pos += bcnt;
        } else if (s->xmit_pos >= 0) {
            int bcnt = 4096 - GET_FIELD(tmd.length, TMDL, BCNT);
            s->phys_mem_read(s->dma_opaque, PHYSADDR(s, tmd.tbadr),
                             s->buffer + s->xmit_pos, bcnt, CSR_BSWP(s));
            s->xmit_pos += bcnt;
#ifdef PCNET_DEBUG
            printf("pcnet_transmit size=%d\n", s->xmit_pos);
#endif
            if (CSR_LOOP(s)) {
                if (BCR_SWSTYLE(s) == 1)
                    add_crc = !GET_FIELD(tmd.status, TMDS, NOFCS);
                s->looptest = add_crc ? PCNET_LOOPTEST_CRC : PCNET_LOOPTEST_NOCRC;
                pcnet_receive(s, s->buffer, s->xmit_pos);
                s->looptest = 0;
            } else
                if (s->vc)
                    qemu_send_packet(s->vc, s->buffer, s->xmit_pos);

            s->csr[0] &= ~0x0008;   /* clear TDMD */
            s->csr[4] |= 0x0004;    /* set TXSTRT */
            s->xmit_pos = -1;
        }

        SET_FIELD(&tmd.status, TMDS, OWN, 0);
        TMDSTORE(&tmd, PHYSADDR(s,CSR_CXDA(s)));
        if (!CSR_TOKINTD(s) || (CSR_LTINTEN(s) && GET_FIELD(tmd.status, TMDS, LTINT)))
            s->csr[0] |= 0x0200;    /* set TINT */

        if (CSR_XMTRC(s)<=1)
            CSR_XMTRC(s) = CSR_XMTRL(s);
        else
            CSR_XMTRC(s)--;
        if (count--)
            goto txagain;

    } else
    if (s->xmit_pos >= 0) {
        struct pcnet_TMD tmd;
        TMDLOAD(&tmd, xmit_cxda);
        SET_FIELD(&tmd.misc, TMDM, BUFF, 1);
        SET_FIELD(&tmd.misc, TMDM, UFLO, 1);
        SET_FIELD(&tmd.status, TMDS, ERR, 1);
        SET_FIELD(&tmd.status, TMDS, OWN, 0);
        TMDSTORE(&tmd, xmit_cxda);
        s->csr[0] |= 0x0200;    /* set TINT */
        if (!CSR_DXSUFLO(s)) {
            s->csr[0] &= ~0x0010;
        } else
        if (count--)
          goto txagain;
    }

    s->tx_busy = 0;
}

static void pcnet_poll(PCNetState *s)
{
    if (CSR_RXON(s)) {
        pcnet_rdte_poll(s);
    }

    if (CSR_TDMD(s) ||
        (CSR_TXON(s) && !CSR_DPOLL(s) && pcnet_tdte_poll(s)))
    {
        /* prevent recursion */
        if (s->tx_busy)
            return;

        pcnet_transmit(s);
    }
}

static void pcnet_poll_timer(void *opaque)
{
    PCNetState *s = opaque;

    qemu_del_timer(s->poll_timer);

    if (CSR_TDMD(s)) {
        pcnet_transmit(s);
    }

    pcnet_update_irq(s);

    if (!CSR_STOP(s) && !CSR_SPND(s) && !CSR_DPOLL(s)) {
        uint64_t now = qemu_get_clock(vm_clock) * 33;
        if (!s->timer || !now)
            s->timer = now;
        else {
            uint64_t t = now - s->timer + CSR_POLL(s);
            if (t > 0xffffLL) {
                pcnet_poll(s);
                CSR_POLL(s) = CSR_PINT(s);
            } else
                CSR_POLL(s) = t;
        }
        qemu_mod_timer(s->poll_timer,
            pcnet_get_next_poll_time(s,qemu_get_clock(vm_clock)));
    }
}


static void pcnet_csr_writew(PCNetState *s, uint32_t rap, uint32_t new_value)
{
    uint16_t val = new_value;
#ifdef PCNET_DEBUG_CSR
    printf("pcnet_csr_writew rap=%d val=0x%04x\n", rap, val);
#endif
    switch (rap) {
    case 0:
        s->csr[0] &= ~(val & 0x7f00); /* Clear any interrupt flags */

        s->csr[0] = (s->csr[0] & ~0x0040) | (val & 0x0048);

        val = (val & 0x007f) | (s->csr[0] & 0x7f00);

        /* IFF STOP, STRT and INIT are set, clear STRT and INIT */
        if ((val&7) == 7)
          val &= ~3;

        if (!CSR_STOP(s) && (val & 4))
            pcnet_stop(s);

        if (!CSR_INIT(s) && (val & 1))
            pcnet_init(s);

        if (!CSR_STRT(s) && (val & 2))
            pcnet_start(s);

        if (CSR_TDMD(s))
            pcnet_transmit(s);

        return;
    case 1:
    case 2:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 18: /* CRBAL */
    case 19: /* CRBAU */
    case 20: /* CXBAL */
    case 21: /* CXBAU */
    case 22: /* NRBAU */
    case 23: /* NRBAU */
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40: /* CRBC */
    case 41:
    case 42: /* CXBC */
    case 43:
    case 44:
    case 45:
    case 46: /* POLL */
    case 47: /* POLLINT */
    case 72:
    case 74:
    case 76: /* RCVRL */
    case 78: /* XMTRL */
    case 112:
       if (CSR_STOP(s) || CSR_SPND(s))
           break;
       return;
    case 3:
        break;
    case 4:
        s->csr[4] &= ~(val & 0x026a);
        val &= ~0x026a; val |= s->csr[4] & 0x026a;
        break;
    case 5:
        s->csr[5] &= ~(val & 0x0a90);
        val &= ~0x0a90; val |= s->csr[5] & 0x0a90;
        break;
    case 16:
        pcnet_csr_writew(s,1,val);
        return;
    case 17:
        pcnet_csr_writew(s,2,val);
        return;
    case 58:
        pcnet_bcr_writew(s,BCR_SWS,val);
        break;
    default:
        return;
    }
    s->csr[rap] = val;
}

static uint32_t pcnet_csr_readw(PCNetState *s, uint32_t rap)
{
    uint32_t val;
    switch (rap) {
    case 0:
        pcnet_update_irq(s);
        val = s->csr[0];
        val |= (val & 0x7800) ? 0x8000 : 0;
        break;
    case 16:
        return pcnet_csr_readw(s,1);
    case 17:
        return pcnet_csr_readw(s,2);
    case 58:
        return pcnet_bcr_readw(s,BCR_SWS);
    case 88:
        val = s->csr[89];
        val <<= 16;
        val |= s->csr[88];
        break;
    default:
        val = s->csr[rap];
    }
#ifdef PCNET_DEBUG_CSR
    printf("pcnet_csr_readw rap=%d val=0x%04x\n", rap, val);
#endif
    return val;
}

static void pcnet_bcr_writew(PCNetState *s, uint32_t rap, uint32_t val)
{
    rap &= 127;
#ifdef PCNET_DEBUG_BCR
    printf("pcnet_bcr_writew rap=%d val=0x%04x\n", rap, val);
#endif
    switch (rap) {
    case BCR_SWS:
        if (!(CSR_STOP(s) || CSR_SPND(s)))
            return;
        val &= ~0x0300;
        switch (val & 0x00ff) {
        case 0:
            val |= 0x0200;
            break;
        case 1:
            val |= 0x0100;
            break;
        case 2:
        case 3:
            val |= 0x0300;
            break;
        default:
            printf("Bad SWSTYLE=0x%02x\n", val & 0xff);
            val = 0x0200;
            break;
        }
#ifdef PCNET_DEBUG
       printf("BCR_SWS=0x%04x\n", val);
#endif
    case BCR_LNKST:
    case BCR_LED1:
    case BCR_LED2:
    case BCR_LED3:
    case BCR_MC:
    case BCR_FDC:
    case BCR_BSBC:
    case BCR_EECAS:
    case BCR_PLAT:
        s->bcr[rap] = val;
        break;
    default:
        break;
    }
}

static uint32_t pcnet_bcr_readw(PCNetState *s, uint32_t rap)
{
    uint32_t val;
    rap &= 127;
    switch (rap) {
    case BCR_LNKST:
    case BCR_LED1:
    case BCR_LED2:
    case BCR_LED3:
        val = s->bcr[rap] & ~0x8000;
        val |= (val & 0x017f & s->lnkst) ? 0x8000 : 0;
        break;
    default:
        val = rap < 32 ? s->bcr[rap] : 0;
        break;
    }
#ifdef PCNET_DEBUG_BCR
    printf("pcnet_bcr_readw rap=%d val=0x%04x\n", rap, val);
#endif
    return val;
}

static void pcnet_h_reset(void *opaque)
{
    PCNetState *s = opaque;
    int i;
    uint16_t checksum;

    /* Initialize the PROM */

    memcpy(s->prom, s->macaddr, 6);
    s->prom[12] = s->prom[13] = 0x00;
    s->prom[14] = s->prom[15] = 0x57;

    for (i = 0,checksum = 0; i < 16; i++)
        checksum += s->prom[i];
    *(uint16_t *)&s->prom[12] = cpu_to_le16(checksum);


    s->bcr[BCR_MSRDA] = 0x0005;
    s->bcr[BCR_MSWRA] = 0x0005;
    s->bcr[BCR_MC   ] = 0x0002;
    s->bcr[BCR_LNKST] = 0x00c0;
    s->bcr[BCR_LED1 ] = 0x0084;
    s->bcr[BCR_LED2 ] = 0x0088;
    s->bcr[BCR_LED3 ] = 0x0090;
    s->bcr[BCR_FDC  ] = 0x0000;
    s->bcr[BCR_BSBC ] = 0x9001;
    s->bcr[BCR_EECAS] = 0x0002;
    s->bcr[BCR_SWS  ] = 0x0200;
    s->bcr[BCR_PLAT ] = 0xff06;

    pcnet_s_reset(s);
}

static void pcnet_aprom_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCNetState *s = opaque;
#ifdef PCNET_DEBUG
    printf("pcnet_aprom_writeb addr=0x%08x val=0x%02x\n", addr, val);
#endif
    /* Check APROMWE bit to enable write access */
    if (pcnet_bcr_readw(s,2) & 0x80)
        s->prom[addr & 15] = val;
}

static uint32_t pcnet_aprom_readb(void *opaque, uint32_t addr)
{
    PCNetState *s = opaque;
    uint32_t val = s->prom[addr &= 15];
#ifdef PCNET_DEBUG
    printf("pcnet_aprom_readb addr=0x%08x val=0x%02x\n", addr, val);
#endif
    return val;
}

static void pcnet_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    PCNetState *s = opaque;
    pcnet_poll_timer(s);
#ifdef PCNET_DEBUG_IO
    printf("pcnet_ioport_writew addr=0x%08x val=0x%04x\n", addr, val);
#endif
    if (!BCR_DWIO(s)) {
        switch (addr & 0x0f) {
        case 0x00: /* RDP */
            pcnet_csr_writew(s, s->rap, val);
            break;
        case 0x02:
            s->rap = val & 0x7f;
            break;
        case 0x06:
            pcnet_bcr_writew(s, s->rap, val);
            break;
        }
    }
    pcnet_update_irq(s);
}

static uint32_t pcnet_ioport_readw(void *opaque, uint32_t addr)
{
    PCNetState *s = opaque;
    uint32_t val = -1;
    pcnet_poll_timer(s);
    if (!BCR_DWIO(s)) {
        switch (addr & 0x0f) {
        case 0x00: /* RDP */
            val = pcnet_csr_readw(s, s->rap);
            break;
        case 0x02:
            val = s->rap;
            break;
        case 0x04:
            pcnet_s_reset(s);
            val = 0;
            break;
        case 0x06:
            val = pcnet_bcr_readw(s, s->rap);
            break;
        }
    }
    pcnet_update_irq(s);
#ifdef PCNET_DEBUG_IO
    printf("pcnet_ioport_readw addr=0x%08x val=0x%04x\n", addr, val & 0xffff);
#endif
    return val;
}

static void pcnet_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    PCNetState *s = opaque;
    pcnet_poll_timer(s);
#ifdef PCNET_DEBUG_IO
    printf("pcnet_ioport_writel addr=0x%08x val=0x%08x\n", addr, val);
#endif
    if (BCR_DWIO(s)) {
        switch (addr & 0x0f) {
        case 0x00: /* RDP */
            pcnet_csr_writew(s, s->rap, val & 0xffff);
            break;
        case 0x04:
            s->rap = val & 0x7f;
            break;
        case 0x0c:
            pcnet_bcr_writew(s, s->rap, val & 0xffff);
            break;
        }
    } else
    if ((addr & 0x0f) == 0) {
        /* switch device to dword i/o mode */
        pcnet_bcr_writew(s, BCR_BSBC, pcnet_bcr_readw(s, BCR_BSBC) | 0x0080);
#ifdef PCNET_DEBUG_IO
        printf("device switched into dword i/o mode\n");
#endif
    }
    pcnet_update_irq(s);
}

static uint32_t pcnet_ioport_readl(void *opaque, uint32_t addr)
{
    PCNetState *s = opaque;
    uint32_t val = -1;
    pcnet_poll_timer(s);
    if (BCR_DWIO(s)) {
        switch (addr & 0x0f) {
        case 0x00: /* RDP */
            val = pcnet_csr_readw(s, s->rap);
            break;
        case 0x04:
            val = s->rap;
            break;
        case 0x08:
            pcnet_s_reset(s);
            val = 0;
            break;
        case 0x0c:
            val = pcnet_bcr_readw(s, s->rap);
            break;
        }
    }
    pcnet_update_irq(s);
#ifdef PCNET_DEBUG_IO
    printf("pcnet_ioport_readl addr=0x%08x val=0x%08x\n", addr, val);
#endif
    return val;
}

static void pcnet_ioport_map(PCIDevice *pci_dev, int region_num,
                             uint32_t addr, uint32_t size, int type)
{
    PCNetState *d = &((PCIPCNetState *)pci_dev)->state;

#ifdef PCNET_DEBUG_IO
    printf("pcnet_ioport_map addr=0x%04x size=0x%04x\n", addr, size);
#endif

    register_ioport_write(addr, 16, 1, pcnet_aprom_writeb, d);
    register_ioport_read(addr, 16, 1, pcnet_aprom_readb, d);

    register_ioport_write(addr + 0x10, 0x10, 2, pcnet_ioport_writew, d);
    register_ioport_read(addr + 0x10, 0x10, 2, pcnet_ioport_readw, d);
    register_ioport_write(addr + 0x10, 0x10, 4, pcnet_ioport_writel, d);
    register_ioport_read(addr + 0x10, 0x10, 4, pcnet_ioport_readl, d);
}

static void pcnet_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_writeb addr=0x" TARGET_FMT_plx" val=0x%02x\n", addr,
           val);
#endif
    if (!(addr & 0x10))
        pcnet_aprom_writeb(d, addr & 0x0f, val);
}

static uint32_t pcnet_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    PCNetState *d = opaque;
    uint32_t val = -1;
    if (!(addr & 0x10))
        val = pcnet_aprom_readb(d, addr & 0x0f);
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_readb addr=0x" TARGET_FMT_plx " val=0x%02x\n", addr,
           val & 0xff);
#endif
    return val;
}

static void pcnet_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_writew addr=0x" TARGET_FMT_plx " val=0x%04x\n", addr,
           val);
#endif
    if (addr & 0x10)
        pcnet_ioport_writew(d, addr & 0x0f, val);
    else {
        addr &= 0x0f;
        pcnet_aprom_writeb(d, addr, val & 0xff);
        pcnet_aprom_writeb(d, addr+1, (val & 0xff00) >> 8);
    }
}

static uint32_t pcnet_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    PCNetState *d = opaque;
    uint32_t val = -1;
    if (addr & 0x10)
        val = pcnet_ioport_readw(d, addr & 0x0f);
    else {
        addr &= 0x0f;
        val = pcnet_aprom_readb(d, addr+1);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr);
    }
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_readw addr=0x" TARGET_FMT_plx" val = 0x%04x\n", addr,
           val & 0xffff);
#endif
    return val;
}

static void pcnet_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_writel addr=0x" TARGET_FMT_plx" val=0x%08x\n", addr,
           val);
#endif
    if (addr & 0x10)
        pcnet_ioport_writel(d, addr & 0x0f, val);
    else {
        addr &= 0x0f;
        pcnet_aprom_writeb(d, addr, val & 0xff);
        pcnet_aprom_writeb(d, addr+1, (val & 0xff00) >> 8);
        pcnet_aprom_writeb(d, addr+2, (val & 0xff0000) >> 16);
        pcnet_aprom_writeb(d, addr+3, (val & 0xff000000) >> 24);
    }
}

static uint32_t pcnet_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    PCNetState *d = opaque;
    uint32_t val;
    if (addr & 0x10)
        val = pcnet_ioport_readl(d, addr & 0x0f);
    else {
        addr &= 0x0f;
        val = pcnet_aprom_readb(d, addr+3);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr+2);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr+1);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr);
    }
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_readl addr=0x" TARGET_FMT_plx " val=0x%08x\n", addr,
           val);
#endif
    return val;
}


static void pcnet_save(QEMUFile *f, void *opaque)
{
    PCNetState *s = opaque;
    unsigned int i;

    if (s->pci_dev)
        pci_device_save(s->pci_dev, f);

    qemu_put_sbe32(f, s->rap);
    qemu_put_sbe32(f, s->isr);
    qemu_put_sbe32(f, s->lnkst);
    qemu_put_be32s(f, &s->rdra);
    qemu_put_be32s(f, &s->tdra);
    qemu_put_buffer(f, s->prom, 16);
    for (i = 0; i < 128; i++)
        qemu_put_be16s(f, &s->csr[i]);
    for (i = 0; i < 32; i++)
        qemu_put_be16s(f, &s->bcr[i]);
    qemu_put_be64s(f, &s->timer);
    qemu_put_sbe32(f, s->xmit_pos);
    qemu_put_sbe32(f, s->recv_pos);
    qemu_put_buffer(f, s->buffer, 4096);
    qemu_put_sbe32(f, s->tx_busy);
    qemu_put_timer(f, s->poll_timer);
}

static int pcnet_load(QEMUFile *f, void *opaque, int version_id)
{
    PCNetState *s = opaque;
    int i, ret;

    if (version_id != 2)
        return -EINVAL;

    if (s->pci_dev) {
        ret = pci_device_load(s->pci_dev, f);
        if (ret < 0)
            return ret;
    }

    qemu_get_sbe32s(f, &s->rap);
    qemu_get_sbe32s(f, &s->isr);
    qemu_get_sbe32s(f, &s->lnkst);
    qemu_get_be32s(f, &s->rdra);
    qemu_get_be32s(f, &s->tdra);
    qemu_get_buffer(f, s->prom, 16);
    for (i = 0; i < 128; i++)
        qemu_get_be16s(f, &s->csr[i]);
    for (i = 0; i < 32; i++)
        qemu_get_be16s(f, &s->bcr[i]);
    qemu_get_be64s(f, &s->timer);
    qemu_get_sbe32s(f, &s->xmit_pos);
    qemu_get_sbe32s(f, &s->recv_pos);
    qemu_get_buffer(f, s->buffer, 4096);
    qemu_get_sbe32s(f, &s->tx_busy);
    qemu_get_timer(f, s->poll_timer);

    return 0;
}

static void pcnet_common_cleanup(PCNetState *d)
{
    unregister_savevm("pcnet", d);

    qemu_del_timer(d->poll_timer);
    qemu_free_timer(d->poll_timer);
}

static void pcnet_common_init(DeviceState *dev, PCNetState *s,
                              NetCleanup *cleanup)
{
    s->poll_timer = qemu_new_timer(vm_clock, pcnet_poll_timer, s);

    qdev_get_macaddr(dev, s->macaddr);
    s->vc = qdev_get_vlan_client(dev,
                                 pcnet_receive, pcnet_can_receive,
                                 cleanup, s);
    pcnet_h_reset(s);
    register_savevm("pcnet", -1, 2, pcnet_save, pcnet_load, s);
}

/* PCI interface */

static CPUWriteMemoryFunc *pcnet_mmio_write[] = {
    (CPUWriteMemoryFunc *)&pcnet_mmio_writeb,
    (CPUWriteMemoryFunc *)&pcnet_mmio_writew,
    (CPUWriteMemoryFunc *)&pcnet_mmio_writel
};

static CPUReadMemoryFunc *pcnet_mmio_read[] = {
    (CPUReadMemoryFunc *)&pcnet_mmio_readb,
    (CPUReadMemoryFunc *)&pcnet_mmio_readw,
    (CPUReadMemoryFunc *)&pcnet_mmio_readl
};

static void pcnet_mmio_map(PCIDevice *pci_dev, int region_num,
                            uint32_t addr, uint32_t size, int type)
{
    PCIPCNetState *d = (PCIPCNetState *)pci_dev;

#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_map addr=0x%08x 0x%08x\n", addr, size);
#endif

    cpu_register_physical_memory(addr, PCNET_PNPMMIO_SIZE, d->state.mmio_index);
}

static void pci_physical_memory_write(void *dma_opaque, target_phys_addr_t addr,
                                      uint8_t *buf, int len, int do_bswap)
{
    cpu_physical_memory_write(addr, buf, len);
}

static void pci_physical_memory_read(void *dma_opaque, target_phys_addr_t addr,
                                     uint8_t *buf, int len, int do_bswap)
{
    cpu_physical_memory_read(addr, buf, len);
}

static void pci_pcnet_cleanup(VLANClientState *vc)
{
    PCNetState *d = vc->opaque;

    pcnet_common_cleanup(d);
}

static int pci_pcnet_uninit(PCIDevice *dev)
{
    PCIPCNetState *d = (PCIPCNetState *)dev;

    cpu_unregister_io_memory(d->state.mmio_index);

    return 0;
}

static void pci_pcnet_init(PCIDevice *pci_dev)
{
    PCIPCNetState *d = (PCIPCNetState *)pci_dev;
    PCNetState *s = &d->state;
    uint8_t *pci_conf;

#if 0
    printf("sizeof(RMD)=%d, sizeof(TMD)=%d\n",
        sizeof(struct pcnet_RMD), sizeof(struct pcnet_TMD));
#endif

    pci_dev->unregister = pci_pcnet_uninit;

    pci_conf = pci_dev->config;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_AMD);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_AMD_LANCE);
    *(uint16_t *)&pci_conf[0x04] = cpu_to_le16(0x0007);
    *(uint16_t *)&pci_conf[0x06] = cpu_to_le16(0x0280);
    pci_conf[0x08] = 0x10;
    pci_conf[0x09] = 0x00;
    pci_config_set_class(pci_conf, PCI_CLASS_NETWORK_ETHERNET);
    pci_conf[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type

    *(uint32_t *)&pci_conf[0x10] = cpu_to_le32(0x00000001);
    *(uint32_t *)&pci_conf[0x14] = cpu_to_le32(0x00000000);

    pci_conf[0x3d] = 1; // interrupt pin 0
    pci_conf[0x3e] = 0x06;
    pci_conf[0x3f] = 0xff;

    /* Handler for memory-mapped I/O */
    s->mmio_index =
      cpu_register_io_memory(0, pcnet_mmio_read, pcnet_mmio_write, &d->state);

    pci_register_io_region((PCIDevice *)d, 0, PCNET_IOPORT_SIZE,
                           PCI_ADDRESS_SPACE_IO, pcnet_ioport_map);

    pci_register_io_region((PCIDevice *)d, 1, PCNET_PNPMMIO_SIZE,
                           PCI_ADDRESS_SPACE_MEM, pcnet_mmio_map);

    s->irq = pci_dev->irq[0];
    s->phys_mem_read = pci_physical_memory_read;
    s->phys_mem_write = pci_physical_memory_write;
    s->pci_dev = pci_dev;

    pcnet_common_init(&pci_dev->qdev, s, pci_pcnet_cleanup);
}

/* SPARC32 interface */

#if defined (TARGET_SPARC) && !defined(TARGET_SPARC64) // Avoid compile failure
#include "sun4m.h"

static void parent_lance_reset(void *opaque, int irq, int level)
{
    SysBusPCNetState *d = opaque;
    if (level)
        pcnet_h_reset(&d->state);
}

static void lance_mem_writew(void *opaque, target_phys_addr_t addr,
                             uint32_t val)
{
    SysBusPCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("lance_mem_writew addr=" TARGET_FMT_plx " val=0x%04x\n", addr,
           val & 0xffff);
#endif
    pcnet_ioport_writew(&d->state, addr, val & 0xffff);
}

static uint32_t lance_mem_readw(void *opaque, target_phys_addr_t addr)
{
    SysBusPCNetState *d = opaque;
    uint32_t val;

    val = pcnet_ioport_readw(&d->state, addr);
#ifdef PCNET_DEBUG_IO
    printf("lance_mem_readw addr=" TARGET_FMT_plx " val = 0x%04x\n", addr,
           val & 0xffff);
#endif

    return val & 0xffff;
}

static CPUReadMemoryFunc *lance_mem_read[3] = {
    NULL,
    lance_mem_readw,
    NULL,
};

static CPUWriteMemoryFunc *lance_mem_write[3] = {
    NULL,
    lance_mem_writew,
    NULL,
};

static void lance_cleanup(VLANClientState *vc)
{
    PCNetState *d = vc->opaque;

    pcnet_common_cleanup(d);
}

static void lance_init(SysBusDevice *dev)
{
    SysBusPCNetState *d = FROM_SYSBUS(SysBusPCNetState, dev);
    PCNetState *s = &d->state;

    s->mmio_index =
        cpu_register_io_memory(0, lance_mem_read, lance_mem_write, d);

    s->dma_opaque = qdev_get_prop_ptr(&dev->qdev, "dma");

    qdev_init_irq_sink(&dev->qdev, parent_lance_reset, 1);

    sysbus_init_mmio(dev, 4, s->mmio_index);

    sysbus_init_irq(dev, &s->irq);

    s->phys_mem_read = ledma_memory_read;
    s->phys_mem_write = ledma_memory_write;

    pcnet_common_init(&dev->qdev, s, lance_cleanup);
}
#endif /* TARGET_SPARC */

static void pcnet_register_devices(void)
{
    pci_qdev_register("pcnet", sizeof(PCIPCNetState), pci_pcnet_init);
#if defined (TARGET_SPARC) && !defined(TARGET_SPARC64)
    sysbus_register_dev("lance", sizeof(SysBusPCNetState), lance_init);
#endif
}

device_init(pcnet_register_devices)
