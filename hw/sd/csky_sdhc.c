/*
 * CSKY SD Host Controller emulation for CSKY V2.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "qemu/log.h"
#include "qapi/error.h"

#define TYPE_CSKY_SDHC "csky_sdhc"
#define CSKY_SDHC(obj) OBJECT_CHECK(csky_sdhc_state, (obj), TYPE_CSKY_SDHC)

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    BlockDriverState *bdrv;
    SDState *card;
    uint32_t Ctrl;
    uint32_t Pow_En;
    uint32_t Clk_Div;
    uint32_t Clk_Src;
    uint32_t Clk_En;
    uint32_t Timeout;
    uint32_t Card_Type;
    uint32_t Blk_Size;
    uint32_t Byte_Cnt;
    uint32_t Int_Mask;
    uint32_t Cmd_Arg;
    uint32_t Cmd;
    uint32_t Resp[4];
    uint32_t Mask_Int_Stas;
    uint32_t Raw_Int_Stas;
    uint32_t Status;
    uint32_t Fifoth;
    uint32_t Card_Detc;
    uint32_t Write_Protec;
    uint32_t Gpio;
    uint32_t Tccbc;
    uint32_t Thbbc;
    uint32_t Deb_Cnt;
    uint32_t Usr_ID;
    uint32_t Ver_ID;
    uint32_t HW_Config;
    uint32_t Bus_Mode;
    uint32_t Poll_Demad;
    uint32_t Descrip_LBA;
    uint32_t IDMAC_Stas;
    uint32_t IDMAC_Int_En;
    uint32_t Cur_Host_Des_Addr;
    uint32_t Cur_Buf_Des_Addr;
#define FIFODEPTH    0x80         /* FIFO depth */
    uint32_t Fifo[FIFODEPTH];
    uint8_t  Fifo_start;

    qemu_irq irq;
    qemu_irq *dma;
} csky_sdhc_state;

static const VMStateDescription vmstate_csky_sdhc = {
    .name = TYPE_CSKY_SDHC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(Ctrl, csky_sdhc_state),
        VMSTATE_UINT32(Pow_En, csky_sdhc_state),
        VMSTATE_UINT32(Clk_Div, csky_sdhc_state),
        VMSTATE_UINT32(Clk_En, csky_sdhc_state),
        VMSTATE_UINT32(Timeout, csky_sdhc_state),
        VMSTATE_UINT32(Card_Type, csky_sdhc_state),
        VMSTATE_UINT32(Blk_Size, csky_sdhc_state),
        VMSTATE_UINT32(Byte_Cnt, csky_sdhc_state),
        VMSTATE_UINT32(Int_Mask, csky_sdhc_state),
        VMSTATE_UINT32(Cmd_Arg, csky_sdhc_state),
        VMSTATE_UINT32(Cmd, csky_sdhc_state),
        VMSTATE_UINT32_ARRAY(Resp, csky_sdhc_state, 4),
        VMSTATE_UINT32(Mask_Int_Stas, csky_sdhc_state),
        VMSTATE_UINT32(Raw_Int_Stas, csky_sdhc_state),
        VMSTATE_UINT32(Status, csky_sdhc_state),
        VMSTATE_UINT32(Fifoth, csky_sdhc_state),
        VMSTATE_UINT32(Card_Detc, csky_sdhc_state),
        VMSTATE_UINT32(Write_Protec, csky_sdhc_state),
        VMSTATE_UINT32(Gpio, csky_sdhc_state),
        VMSTATE_UINT32(Tccbc, csky_sdhc_state),
        VMSTATE_UINT32(Thbbc, csky_sdhc_state),
        VMSTATE_UINT32(Deb_Cnt, csky_sdhc_state),
        VMSTATE_UINT32(Usr_ID, csky_sdhc_state),
        VMSTATE_UINT32(Ver_ID, csky_sdhc_state),
        VMSTATE_UINT32(HW_Config, csky_sdhc_state),
        VMSTATE_UINT32(Bus_Mode, csky_sdhc_state),
        VMSTATE_UINT32(Poll_Demad, csky_sdhc_state),
        VMSTATE_UINT32(Descrip_LBA, csky_sdhc_state),
        VMSTATE_UINT32(IDMAC_Stas, csky_sdhc_state),
        VMSTATE_UINT32(IDMAC_Int_En, csky_sdhc_state),
        VMSTATE_UINT32(Cur_Host_Des_Addr, csky_sdhc_state),
        VMSTATE_UINT32(Cur_Buf_Des_Addr, csky_sdhc_state),
        VMSTATE_UINT32_ARRAY(Fifo, csky_sdhc_state, 0x80),
        VMSTATE_UINT8(Fifo_start, csky_sdhc_state),
        VMSTATE_END_OF_LIST()
    }
};

#define CTRL         0x00       /* SDHC control register */
#define POW_EN       0x04       /* SD POWER CONTROL */
#define CLK_DIV      0x08       /* CLOCK DIVIDER */
#define CLK_SRC      0x0C       /* SD CLOCK SOURCE */
#define CLK_EN       0x10       /* CLOCK ENABLE */
#define TIMEOUT      0x14       /* TIME OUT */
#define CTYPE        0x18       /* CARD TYPE */
#define BLKSIZE      0x1C       /* BLOCK SIZE */
#define BYTCNT       0x20       /* BYTE COUNT */
#define INTMASK      0x24       /* INTERRUPT MASK */
#define CMDARG       0x28       /* COMMAND ARGUMENT */
#define CMD          0x2c       /* command */
#define RESP0        0x30       /* RESPONSE 0 */
#define RESP1        0x34       /* RESPONSE 1 */
#define RESP2        0x38       /* RESPONSE 2 */
#define RESP3        0x3C       /* RESPONSE 3 */
#define MASK_INT_STS 0x40       /* MASKED INTERRUPT SD_STATUS */
#define RAW_INT_STS  0x44       /* RAW INTERRUPT SD_STATUS */
#define SD_STATUS    0x48       /* SD_STATUS */
#define FIFOTH       0x4C       /* FIFO WATER MARK */
#define CDET         0x50       /* CARD DETECT */
#define WP           0x54       /* WRITE PROTECT */
#define GPIO         0x58       /* general purpose input and output register */
#define TCCBC        0x5C       /* TRANSFERRED CIU CARD BYTE COUNT */
#define THBBC        0x60       /* TRANSFERRED HOST TO BIU BYTE COUNT */
#define DEBCNT       0x64       /* DEBOUNCE COUNT */
#define UID          0x68       /* user ID */
#define VID          0x6c       /* version ID */
#define HCIG         0x70       /* HARDWARE CONFIGURATION */
#define BMD          0x80       /* BUS MODE */
#define PDMD         0x84       /* POLL DEMAND */
#define DLBA         0x88       /* DESCRIPTOR LIST BASE ADDRESS */
#define IDMACS       0x8c       /* internal dmac status */
#define IDMACIEN     0x90       /* INTERNAL DMAC INTERRUPT ENABLE */
#define CHDA         0x94       /* CURRENT HOST DESCRIPTOR ADDRESS */
#define CBDA         0x98       /* CURRENT BUFFER DESCRIPTOR ADDRESS */
#define FIFOADDR     0x100      /* FIFO ADDRESS */

/*================================================================
 *       BIT
 *---------------------------------------------------------
 */
#define BIT0        0x00000001
#define BIT1        0x00000002
#define BIT2        0x00000004
#define BIT3        0x00000008
#define BIT4        0x00000010
#define BIT5        0x00000020
#define BIT6        0x00000040
#define BIT7        0x00000080

#define BIT8        0x00000100
#define BIT9        0x00000200
#define BIT10       0x00000400
#define BIT11       0x00000800
#define BIT12       0x00001000
#define BIT13       0x00002000
#define BIT14       0x00004000
#define BIT15       0x00008000

#define BIT16       0x00010000
#define BIT17       0x00020000
#define BIT18       0x00040000
#define BIT19       0x00080000
#define BIT20       0x00100000
#define BIT21       0x00200000
#define BIT22       0x00400000
#define BIT23       0x00800000

#define BIT24       0x01000000
#define BIT25       0x02000000
#define BIT26       0x04000000
#define BIT27       0x08000000
#define BIT28       0x10000000
#define BIT29       0x20000000
#define BIT30       0x40000000
#define BIT31       0x80000000

/*********************************************************
 *  FIELD
 */
#define FIFO_CNT     0x3ffe0000 /* the vocation of fifocount in status */


/*****************************************************************************
 **
 ** Description: This function is uesed to reset SD host
 **
 *****************************************************************************/

static void csky_sdhc_reset(DeviceState *d)
{
    csky_sdhc_state *host = CSKY_SDHC(d);

    host->Ctrl = 0;
    host->Pow_En = 0x0;
    host->Clk_Div = 0x0;
    host->Clk_Src = 0x0;
    host->Clk_En = 0x0;
    host->Timeout = 0xffffff40;
    host->Card_Type = 0x0;
    host->Blk_Size = 0x200;
    host->Byte_Cnt = 0x200;
    host->Int_Mask = 0x0;
    host->Cmd_Arg = 0x0;
    host->Cmd = 0x0;
    memset(host->Resp, 0, sizeof(host->Resp));
    host->Mask_Int_Stas = 0x0;
    host->Raw_Int_Stas = 0x0;
    host->Status = 0x106;         /* need to be consider */
    host->Fifoth &= 0x000f0000;
    host->Card_Detc = 0xfffffffe;
    host->Tccbc = 0x0;
    host->Thbbc = 0x0;
    host->Deb_Cnt = 0x00ffffff;
    host->Usr_ID = 0x0;
    host->Ver_ID = 0x0;
    host->HW_Config = 0x792cc3;
    host->Bus_Mode = 0x0;
    host->Poll_Demad = 0x0;
    host->Descrip_LBA = 0x0;
    host->IDMAC_Stas = 0x0;
    host->IDMAC_Int_En = 0x0;
    host->Cur_Host_Des_Addr = 0x0;
    host->Cur_Buf_Des_Addr = 0x0;
}

/******************************************************************************
 **
 ** Description:
 This function is uesed to update certain bits related to fifo status
 **
 ******************************************************************************/

static void csky_sdhc_fifolevel_update(csky_sdhc_state *host)
{
    int fifocnt = (host->Status & FIFO_CNT) >> 17;
    int fiforxwm = (host->Fifoth & 0xfff0000) >> 16;
    int fifotxwm = (host->Fifoth & 0xfff);
    int direct = host->Cmd & BIT10;     /* "1" REPRESENT WRITE DATA TO CARD */

    if (host->Ctrl & BIT1) {              /* reset fifo */
        host->Fifo_start = 0;
        host->Status = (~FIFO_CNT) & host->Status;
        host->Status |= BIT2 | BIT1;
        host->Status &= ~(BIT3 | BIT0);
        host->Raw_Int_Stas |= BIT4;
        host->Mask_Int_Stas = host->Raw_Int_Stas & host->Int_Mask;
        return;
    }
    if (!fifocnt) {
        host->Status |= BIT2;           /* FIFO is empty */
    } else {
        host->Status &= ~BIT2;
    }

    if (fifocnt == FIFODEPTH) {           /* FIFO IS FULL */
        host->Status |= BIT3;
    } else {
        host->Status &= ~BIT3;
    }
    /* set RXDR bit */
    if (fifocnt > fiforxwm && !direct) {
        host->Raw_Int_Stas |= BIT5;
    }

    /* set TXDR bit */
    if (fifocnt <= fifotxwm && direct) {
        host->Raw_Int_Stas |= BIT4;
    }

    host->Mask_Int_Stas = host->Raw_Int_Stas & host->Int_Mask;

    if (fifocnt > fiforxwm) {
        host->Status |= BIT0;
    } else {
        host->Status &= ~BIT0;
    }
    if (fifocnt <= fifotxwm) {
        host->Status |= BIT1;
    } else {
        host->Status &= ~BIT1;
    }

    return ;
}


/*
 * Description: update interrupt status and raise interrupt
 *
 */

static void csky_sdhc_interrupts_update(csky_sdhc_state *host)
{
    qemu_set_irq(host->irq, host->Mask_Int_Stas && (host->Ctrl & BIT4));
}

/*
 * Description: transfer data between sdhc and SD card
 */
static void csky_sdhc_transfer(csky_sdhc_state *host)
{
    uint8_t value[4];
    uint32_t value1;
    uint32_t fifocnt = (host->Status & FIFO_CNT) >> 17;
    uint32_t write = host->Cmd & BIT10;
    if (!(host->Cmd & BIT9)) {              /* return if no data expected */
        return;
    }
    if (!write) {         /* read a date from sd card */
        while ((fifocnt < 0x80) && (host->Byte_Cnt > 0)) {
            value[0] = sd_read_data(host->card);
            value[1] = sd_read_data(host->card);
            value[2] = sd_read_data(host->card);
            value[3] = sd_read_data(host->card);
            host->Fifo[(host->Fifo_start + fifocnt) & (FIFODEPTH - 1)] =
                (value[3] << 24) | (value[2] << 16) | (value[1] << 8) |
                value[0];
            fifocnt++;
            host->Byte_Cnt -= 4;
        }
    } else {                            /* write date to SD Card */
        while ((fifocnt > 0) && (host->Byte_Cnt > 0)) {
            value1 = host->Fifo[host->Fifo_start];
            host->Fifo_start = (host->Fifo_start + 1) % FIFODEPTH;
            sd_write_data(host->card, value1);
            sd_write_data(host->card, value1 >> 8);
            sd_write_data(host->card, value1 >> 16);
            sd_write_data(host->card, value1 >> 24);
            fifocnt--;
            host->Byte_Cnt -= 4;
        }
    }

    if (host->Byte_Cnt == 0) {                  /* data transfer over */
        host->Raw_Int_Stas = host->Raw_Int_Stas | BIT3;
        host->Mask_Int_Stas = host->Raw_Int_Stas & host->Int_Mask;
    }
    host->Status = (fifocnt << 17) | (~FIFO_CNT & host->Status);
}
/*
 * Description:  sending commmanfs to sd card
 */
static void csky_sdhc_command(csky_sdhc_state *host)
{
    SDRequest request;
    uint8_t response[16];
    int rlen;

    request.cmd = (uint8_t)(host->Cmd & 0X3F);     /* fetch command index */
    request.arg = host->Cmd_Arg;
    if ((host->Cmd & BIT21) != 0x0) {             /* do not send command */
        host->Cmd = host->Cmd & ~BIT31;           /* clear start_cmd bit */
        return;
    }
    rlen = sd_do_command(host->card, &request, response);
    host->Cmd = host->Cmd & ~BIT31;

    if (host->Cmd & BIT6) {                  /* response expected */
        if (rlen == 4 && !(host->Cmd & BIT7)) {         /* SHORT RESPONSE */
            host->Resp[0] = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8)  |  response[3];     /* bit 0 in Resp
                                                           correspond to LSB */
            host->Resp[1] = host->Resp[2] = host->Resp[3] = 0;
        } else if (rlen == 16 && (host->Cmd & BIT7)) {      /* LONG RESPONSE */
            host->Resp[0] = (response[0] << 24) | (response[1] << 16) |
                (response[2] << 8) |  response[3];
            host->Resp[1] = (response[4] << 24) | (response[5] << 16) |
                (response[6] << 8)  |  response[7];
            host->Resp[2] = (response[8] << 24) | (response[9] << 16) |
                (response[10] << 8)  |  response[11];
            host->Resp[3] = (response[12] << 24) | (response[13] << 16) |
                (response[14] << 8) |  response[15];
        } else {
            host->Raw_Int_Stas |= BIT8;         /* RESPONSE ERROR */
        }
    }
    if ((host->Cmd & BIT12)) {  /* sending CMD12 */
        request.cmd = 12;       /* fetch command index */
        sd_do_command(host->card, &request, response);
    }
    host->Raw_Int_Stas |= BIT2;            /* set command done bit */
    host->Mask_Int_Stas = host->Raw_Int_Stas & host->Int_Mask;
}

static void csky_sdhc_update(void *opaque)
{
    csky_sdhc_state  *host = (csky_sdhc_state  *)opaque;
    csky_sdhc_transfer(host);
    csky_sdhc_fifolevel_update(host);
    csky_sdhc_interrupts_update(host);
}

/*
 *Description: read date in register
 */
static uint64_t csky_sdhc_read(void *opaque, hwaddr offset, unsigned size)
{
    uint32_t i;
    csky_sdhc_state *s = (csky_sdhc_state *) opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_sdhc_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case CTRL: /* SDHC control register */
        return s->Ctrl;

    case POW_EN: /* power enable */
        return s->Pow_En ;

    case CLK_DIV: /* clock divider*/
        return s->Clk_Div;

    case CLK_SRC: /* clock source */
        return s->Clk_Src;

    case CLK_EN: /* clock enable */
        return s->Clk_En;

    case TIMEOUT: /* time out */
        return s->Timeout;

    case CTYPE: /* card type */
        return s->Card_Type;

    case BLKSIZE: /* block size */
        return s->Blk_Size;

    case BYTCNT: /* byte count */
        return  s->Byte_Cnt;

    case INTMASK: /* interrupt mask */
        return s->Int_Mask;

    case CMDARG: /* command argument */
        return s->Cmd_Arg;

    case CMD: /* command */
        return s->Cmd;

    case RESP0: /* response */
    case RESP1: /* response 1 */
    case RESP2: /* response2 */
    case RESP3: /* response 3*/
        return s->Resp[(offset - RESP0) >> 2];
    case MASK_INT_STS:  /*masked interrupted status*/
        return s->Mask_Int_Stas;
    case RAW_INT_STS:   /*raw interrupted status*/
        return s->Raw_Int_Stas;
    case SD_STATUS:   /*status*/
        return s->Status;
    case FIFOTH:   /*FIFO Water mark*/
        return s->Fifoth;
    case CDET:   /*card detect*/
        return s->Card_Detc;
    case WP:   /*write protect*/
        return s->Write_Protec;
    case GPIO:   /*general purpose input/output register*/
        return s->Gpio;
    case TCCBC:   /*transferred CIU card byte count*/
        return s->Tccbc;
    case THBBC:   /*transferred  host to BIU-FIFO byte count*/
        return s->Thbbc;
    case DEBCNT:   /*debounce count*/
        return s->Deb_Cnt;
    case UID:   /*user ID*/
        return s->Usr_ID;
    case VID:   /*version ID*/
        return s->Ver_ID;
    case HCIG:   /*Hardware configuration*/
        return s->HW_Config;
    case BMD:   /*bus mode*/
        return s->Bus_Mode;
    case PDMD:   /*poll demand*/
        return s->Poll_Demad;
    case DLBA:   /*descriptor list base address*/
        return s->Descrip_LBA;
    case IDMACS:   /*internal dmac status*/
        return s->IDMAC_Stas;
    case IDMACIEN:   /*internal dmac interrupt status*/
        return s->IDMAC_Int_En;
    case CHDA:   /*current host descriptor address*/
        return s->Cur_Host_Des_Addr;
    case CBDA:   /*current buffer descriptor address*/
        return s->Cur_Buf_Des_Addr;
    default:
        if (offset >= FIFOADDR) {
            int fifocnt = (s->Status & FIFO_CNT) >> 17;
            if (s->Status & BIT2) {
                s->Raw_Int_Stas = s->Raw_Int_Stas | BIT11;
                s->Mask_Int_Stas = s->Raw_Int_Stas & s->Int_Mask;
                qemu_log_mask(LOG_GUEST_ERROR,
                              "MMC: FIFO underrun\n");
                return 0;
            }
            i = s->Fifo[s->Fifo_start];
            s->Fifo_start = (s->Fifo_start + 1) % FIFODEPTH;
            fifocnt--;
            s->Status = (fifocnt << 17) | (~FIFO_CNT & s->Status);
            csky_sdhc_update(s);
            return i;
        } else {                            /* invalid address */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad register %x\n", __func__, (int)offset);
        }
        return -1;
    }
}

/*
 * Description: to write value to register
 */
static void csky_sdhc_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    uint16_t i;
    csky_sdhc_state *s = (csky_sdhc_state *) opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_sdhc_write: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case CTRL:
        s->Ctrl = value;
        csky_sdhc_update(s);
        s->Ctrl &= ~BIT1;
        s->Ctrl &= ~BIT0;
        break;
    case POW_EN:
        s->Pow_En = value;
        sd_enable(s->card, (s->Pow_En & 1));      /* enable sd card */
        break;
    case CLK_DIV:
        s->Clk_Div = value;
        break;
    case CLK_SRC:
        s->Clk_Src = value;
        break;
    case CLK_EN:
        s->Clk_En = value;
        break;
    case TIMEOUT:
        s->Timeout = value;
        break;
    case CTYPE:
        s->Card_Type = value;
        break;
    case BLKSIZE:
        s->Blk_Size = value;
        break;
    case BYTCNT:
        s->Byte_Cnt = value;
        break;
    case INTMASK:
        s->Int_Mask = value;
        break;
    case CMDARG:
        s->Cmd_Arg = value;
        break;
    case CMD:                                        /* processing command */
        s->Cmd = value;
        for (i = 0; i < 4; i++) {
            s->Resp[i] = 0x0;
        }
        csky_sdhc_command(s);
        csky_sdhc_update(s);
        break;

    case RESP0:
    case RESP1:
    case RESP2:
    case RESP3:
        break;
    case MASK_INT_STS:
        break;
    case RAW_INT_STS:
        s->Raw_Int_Stas &= ~value;
        s->Mask_Int_Stas = s->Raw_Int_Stas & s->Int_Mask;
        break;
    case SD_STATUS:
        break;
    case FIFOTH:
        s->Fifoth = value;
        break;
    case CDET:
        s->Card_Detc = value;
        break;
    case WP:
        break;
    case GPIO:
        s->Gpio = value;
        break;
    case TCCBC:

        break;
    case THBBC:

        break;
    case DEBCNT:
        s->Deb_Cnt = value;
        break;
    case UID:
        s->Usr_ID = value;
    case VID:

        break;
    case HCIG:
        break;
    case BMD:

        break;
    case PDMD:
        s->Poll_Demad = value;
        break;
    case DLBA:
        s->Descrip_LBA = value;
        break;
    case IDMACS:
        s->IDMAC_Stas = value;
        break;
    case IDMACIEN:
        s->IDMAC_Int_En = value;
        break;
    case CHDA:

        break;
    case CBDA:

        break;
    default:
        if (offset >= FIFOADDR) {
            int fifocnt = (s->Status & FIFO_CNT) >> 17;
            if (s->Status & BIT3) {                 /* FIFO is full */
                s->Raw_Int_Stas = s->Raw_Int_Stas | BIT11;
                s->Mask_Int_Stas = s->Raw_Int_Stas & s->Int_Mask;
                qemu_log_mask(LOG_GUEST_ERROR,
                              "MMC: FIFO overrun\n");
                return;
            }
            s->Fifo[(fifocnt + s->Fifo_start) &(FIFODEPTH - 1)] = value;
            fifocnt++;
            /* new filled location in FIFO */
            s->Status = (fifocnt << 17) | (~FIFO_CNT & s->Status);
            csky_sdhc_update(s);
        } else {                            /* invalid address */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bad register %x\n", __func__, (int)offset);
        }
    }
}

static const MemoryRegionOps csky_sdhc_ops = {
    .read = csky_sdhc_read,
    .write = csky_sdhc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void csky_sdhc_init(Object *obj)
{
    csky_sdhc_state *s = CSKY_SDHC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &csky_sdhc_ops, s,
                          TYPE_CSKY_SDHC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void csky_sdhc_realize(DeviceState *dev, Error **errp)
{
    csky_sdhc_state *s = CSKY_SDHC(dev);
    DriveInfo *dinfo;

    /* FIXME use a qdev drive property instead of drive_get_next() */
    dinfo = drive_get_next(IF_SD);
    s->card = sd_init(dinfo ? blk_by_legacy_dinfo(dinfo) : NULL, false);
    if (s->card == NULL) {
        error_setg(errp, "sd_init failed");
    }
}

static void csky_sdhc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->vmsd = &vmstate_csky_sdhc;
    k->reset = csky_sdhc_reset;
    /* Reason: init() method uses drive_get_next() */
    k->user_creatable = false;
    k->realize = csky_sdhc_realize;
}

static const TypeInfo csky_sdhc_info = {
    .name          = TYPE_CSKY_SDHC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_sdhc_state),
    .instance_init = csky_sdhc_init,
    .class_init    = csky_sdhc_class_init,
};

static void csky_sdhc_register_types(void)
{
    type_register_static(&csky_sdhc_info);
}

type_init(csky_sdhc_register_types)
