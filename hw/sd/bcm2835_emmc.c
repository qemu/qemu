/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/sd.h"
#include "sysemu/blockdev.h"

/*
 * Controller registers
 */

#define SDHCI_DMA_ADDRESS   0x00
#define SDHCI_ARGUMENT2     SDHCI_DMA_ADDRESS

#define SDHCI_BLOCK_SIZE    0x04
#define  SDHCI_MAKE_BLKSZ(dma, blksz) (((dma & 0x7) << 12) | (blksz & 0xFFF))

#define SDHCI_BLOCK_COUNT   0x06

#define SDHCI_ARGUMENT      0x08

#define SDHCI_TRANSFER_MODE 0x0C
#define  SDHCI_TRNS_DMA     0x01
#define  SDHCI_TRNS_BLK_CNT_EN  0x02
#define  SDHCI_TRNS_AUTO_CMD12  0x04
#define  SDHCI_TRNS_AUTO_CMD23  0x08
#define  SDHCI_TRNS_READ    0x10
#define  SDHCI_TRNS_MULTI   0x20

#define SDHCI_COMMAND       0x0E
#define  SDHCI_CMD_RESP_MASK    0x03
#define  SDHCI_CMD_CRC      0x08
#define  SDHCI_CMD_INDEX    0x10
#define  SDHCI_CMD_DATA     0x20
#define  SDHCI_CMD_ABORTCMD 0xC0

#define  SDHCI_CMD_RESP_NONE    0x00
#define  SDHCI_CMD_RESP_LONG    0x01
#define  SDHCI_CMD_RESP_SHORT   0x02
#define  SDHCI_CMD_RESP_SHORT_BUSY 0x03

#define SDHCI_MAKE_CMD(c, f) (((c & 0xff) << 8) | (f & 0xff))
#define SDHCI_GET_CMD(c) ((c>>8) & 0x3f)

#define SDHCI_RESPONSE      0x10

#define SDHCI_BUFFER        0x20

#define SDHCI_PRESENT_STATE 0x24
#define  SDHCI_CMD_INHIBIT  0x00000001
#define  SDHCI_DATA_INHIBIT 0x00000002
#define  SDHCI_DOING_WRITE  0x00000100
#define  SDHCI_DOING_READ   0x00000200
#define  SDHCI_SPACE_AVAILABLE  0x00000400
#define  SDHCI_DATA_AVAILABLE   0x00000800
#define  SDHCI_CARD_PRESENT 0x00010000
#define  SDHCI_WRITE_PROTECT    0x00080000
#define  SDHCI_DATA_LVL_MASK    0x00F00000
#define   SDHCI_DATA_LVL_SHIFT  20

#define SDHCI_HOST_CONTROL  0x28
#define  SDHCI_CTRL_LED     0x01
#define  SDHCI_CTRL_4BITBUS 0x02
#define  SDHCI_CTRL_HISPD   0x04
#define  SDHCI_CTRL_DMA_MASK    0x18
#define   SDHCI_CTRL_SDMA   0x00
#define   SDHCI_CTRL_ADMA1  0x08
#define   SDHCI_CTRL_ADMA32 0x10
#define   SDHCI_CTRL_ADMA64 0x18
#define   SDHCI_CTRL_8BITBUS    0x20

#define SDHCI_POWER_CONTROL 0x29
#define  SDHCI_POWER_ON     0x01
#define  SDHCI_POWER_180    0x0A
#define  SDHCI_POWER_300    0x0C
#define  SDHCI_POWER_330    0x0E

#define SDHCI_BLOCK_GAP_CONTROL 0x2A

#define SDHCI_WAKE_UP_CONTROL   0x2B
#define  SDHCI_WAKE_ON_INT  0x01
#define  SDHCI_WAKE_ON_INSERT   0x02
#define  SDHCI_WAKE_ON_REMOVE   0x04

#define SDHCI_CLOCK_CONTROL 0x2C
#define  SDHCI_DIVIDER_SHIFT    8
#define  SDHCI_DIVIDER_HI_SHIFT 6
#define  SDHCI_DIV_MASK 0xFF
#define  SDHCI_DIV_MASK_LEN 8
#define  SDHCI_DIV_HI_MASK  0x300
#define  SDHCI_PROG_CLOCK_MODE  0x0020
#define  SDHCI_CLOCK_CARD_EN    0x0004
#define  SDHCI_CLOCK_INT_STABLE 0x0002
#define  SDHCI_CLOCK_INT_EN 0x0001

#define SDHCI_TIMEOUT_CONTROL   0x2E

#define SDHCI_SOFTWARE_RESET    0x2F
#define  SDHCI_RESET_ALL    0x01
#define  SDHCI_RESET_CMD    0x02
#define  SDHCI_RESET_DATA   0x04

#define SDHCI_INT_STATUS    0x30
#define SDHCI_INT_ENABLE    0x34
#define SDHCI_SIGNAL_ENABLE 0x38
#define  SDHCI_INT_RESPONSE 0x00000001
#define  SDHCI_INT_DATA_END 0x00000002
#define  SDHCI_INT_DMA_END  0x00000008
#define  SDHCI_INT_SPACE_AVAIL  0x00000010
#define  SDHCI_INT_DATA_AVAIL   0x00000020
#define  SDHCI_INT_CARD_INSERT  0x00000040
#define  SDHCI_INT_CARD_REMOVE  0x00000080
#define  SDHCI_INT_CARD_INT 0x00000100
#define  SDHCI_INT_ERROR    0x00008000
#define  SDHCI_INT_TIMEOUT  0x00010000
#define  SDHCI_INT_CRC      0x00020000
#define  SDHCI_INT_END_BIT  0x00040000
#define  SDHCI_INT_INDEX    0x00080000
#define  SDHCI_INT_DATA_TIMEOUT 0x00100000
#define  SDHCI_INT_DATA_CRC 0x00200000
#define  SDHCI_INT_DATA_END_BIT 0x00400000
#define  SDHCI_INT_BUS_POWER    0x00800000
#define  SDHCI_INT_ACMD12ERR    0x01000000
#define  SDHCI_INT_ADMA_ERROR   0x02000000

#define  SDHCI_INT_NORMAL_MASK  0x00007FFF
#define  SDHCI_INT_ERROR_MASK   0xFFFF8000

#define  SDHCI_INT_CMD_MASK (SDHCI_INT_RESPONSE | SDHCI_INT_TIMEOUT | \
        SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX)
#define  SDHCI_INT_DATA_MASK    (SDHCI_INT_DATA_END | SDHCI_INT_DMA_END | \
        SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL | \
        SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC | \
        SDHCI_INT_DATA_END_BIT | SDHCI_INT_ADMA_ERROR)
#define SDHCI_INT_ALL_MASK  ((unsigned int)-1)

#define SDHCI_ACMD12_ERR    0x3C

#define SDHCI_HOST_CONTROL2     0x3E
#define  SDHCI_CTRL_UHS_MASK        0x0007
#define   SDHCI_CTRL_UHS_SDR12      0x0000
#define   SDHCI_CTRL_UHS_SDR25      0x0001
#define   SDHCI_CTRL_UHS_SDR50      0x0002
#define   SDHCI_CTRL_UHS_SDR104     0x0003
#define   SDHCI_CTRL_UHS_DDR50      0x0004
#define  SDHCI_CTRL_VDD_180     0x0008
#define  SDHCI_CTRL_DRV_TYPE_MASK   0x0030
#define   SDHCI_CTRL_DRV_TYPE_B     0x0000
#define   SDHCI_CTRL_DRV_TYPE_A     0x0010
#define   SDHCI_CTRL_DRV_TYPE_C     0x0020
#define   SDHCI_CTRL_DRV_TYPE_D     0x0030
#define  SDHCI_CTRL_EXEC_TUNING     0x0040
#define  SDHCI_CTRL_TUNED_CLK       0x0080
#define  SDHCI_CTRL_PRESET_VAL_ENABLE   0x8000

#define SDHCI_CAPABILITIES  0x40
#define  SDHCI_TIMEOUT_CLK_MASK 0x0000003F
#define  SDHCI_TIMEOUT_CLK_SHIFT 0
#define  SDHCI_TIMEOUT_CLK_UNIT 0x00000080
#define  SDHCI_CLOCK_BASE_MASK  0x00003F00
#define  SDHCI_CLOCK_V3_BASE_MASK   0x0000FF00
#define  SDHCI_CLOCK_BASE_SHIFT 8
#define  SDHCI_MAX_BLOCK_MASK   0x00030000
#define  SDHCI_MAX_BLOCK_SHIFT  16
#define  SDHCI_CAN_DO_8BIT  0x00040000
#define  SDHCI_CAN_DO_ADMA2 0x00080000
#define  SDHCI_CAN_DO_ADMA1 0x00100000
#define  SDHCI_CAN_DO_HISPD 0x00200000
#define  SDHCI_CAN_DO_SDMA  0x00400000
#define  SDHCI_CAN_VDD_330  0x01000000
#define  SDHCI_CAN_VDD_300  0x02000000
#define  SDHCI_CAN_VDD_180  0x04000000
#define  SDHCI_CAN_64BIT    0x10000000

#define  SDHCI_SUPPORT_SDR50    0x00000001
#define  SDHCI_SUPPORT_SDR104   0x00000002
#define  SDHCI_SUPPORT_DDR50    0x00000004
#define  SDHCI_DRIVER_TYPE_A    0x00000010
#define  SDHCI_DRIVER_TYPE_C    0x00000020
#define  SDHCI_DRIVER_TYPE_D    0x00000040
#define  SDHCI_RETUNING_TIMER_COUNT_MASK    0x00000F00
#define  SDHCI_RETUNING_TIMER_COUNT_SHIFT   8
#define  SDHCI_USE_SDR50_TUNING         0x00002000
#define  SDHCI_RETUNING_MODE_MASK       0x0000C000
#define  SDHCI_RETUNING_MODE_SHIFT      14
#define  SDHCI_CLOCK_MUL_MASK   0x00FF0000
#define  SDHCI_CLOCK_MUL_SHIFT  16

#define SDHCI_CAPABILITIES_1    0x44

#define SDHCI_MAX_CURRENT       0x48
#define  SDHCI_MAX_CURRENT_330_MASK 0x0000FF
#define  SDHCI_MAX_CURRENT_330_SHIFT    0
#define  SDHCI_MAX_CURRENT_300_MASK 0x00FF00
#define  SDHCI_MAX_CURRENT_300_SHIFT    8
#define  SDHCI_MAX_CURRENT_180_MASK 0xFF0000
#define  SDHCI_MAX_CURRENT_180_SHIFT    16
#define   SDHCI_MAX_CURRENT_MULTIPLIER  4

/* 4C-4F reserved for more max current */

#define SDHCI_SET_ACMD12_ERROR  0x50
#define SDHCI_SET_INT_ERROR 0x52

#define SDHCI_ADMA_ERROR    0x54

/* 55-57 reserved */

#define SDHCI_ADMA_ADDRESS  0x58

/* 60-FB reserved */

#define SDHCI_SLOT_INT_STATUS   0xFC

#define SDHCI_HOST_VERSION  0xFE
#define  SDHCI_VENDOR_VER_MASK  0xFF00
#define  SDHCI_VENDOR_VER_SHIFT 8
#define  SDHCI_SPEC_VER_MASK    0x00FF
#define  SDHCI_SPEC_VER_SHIFT   0
#define   SDHCI_SPEC_100    0
#define   SDHCI_SPEC_200    1
#define   SDHCI_SPEC_300    2

/*
 * End of controller registers.
 */
#define MMC_VDD_165_195     0x00000080  /* VDD voltage 1.65 - 1.95 */
#define MMC_VDD_20_21       0x00000100  /* VDD voltage 2.0 ~ 2.1 */
#define MMC_VDD_21_22       0x00000200  /* VDD voltage 2.1 ~ 2.2 */
#define MMC_VDD_22_23       0x00000400  /* VDD voltage 2.2 ~ 2.3 */
#define MMC_VDD_23_24       0x00000800  /* VDD voltage 2.3 ~ 2.4 */
#define MMC_VDD_24_25       0x00001000  /* VDD voltage 2.4 ~ 2.5 */
#define MMC_VDD_25_26       0x00002000  /* VDD voltage 2.5 ~ 2.6 */
#define MMC_VDD_26_27       0x00004000  /* VDD voltage 2.6 ~ 2.7 */
#define MMC_VDD_27_28       0x00008000  /* VDD voltage 2.7 ~ 2.8 */
#define MMC_VDD_28_29       0x00010000  /* VDD voltage 2.8 ~ 2.9 */
#define MMC_VDD_29_30       0x00020000  /* VDD voltage 2.9 ~ 3.0 */
#define MMC_VDD_30_31       0x00040000  /* VDD voltage 3.0 ~ 3.1 */
#define MMC_VDD_31_32       0x00080000  /* VDD voltage 3.1 ~ 3.2 */
#define MMC_VDD_32_33       0x00100000  /* VDD voltage 3.2 ~ 3.3 */
#define MMC_VDD_33_34       0x00200000  /* VDD voltage 3.3 ~ 3.4 */
#define MMC_VDD_34_35       0x00400000  /* VDD voltage 3.4 ~ 3.5 */
#define MMC_VDD_35_36       0x00800000  /* VDD voltage 3.5 ~ 3.6 */

#define MMC_CAP_4_BIT_DATA  (1 << 0)    /* Can the host do 4 bit transfers */
#define MMC_CAP_MMC_HIGHSPEED   (1 << 1)    /* Can do MMC high-speed timing */
#define MMC_CAP_SD_HIGHSPEED    (1 << 2)    /* Can do SD high-speed timing */
#define MMC_CAP_SDIO_IRQ    (1 << 3)    /* Can signal pending SDIO IRQs */
#define MMC_CAP_SPI     (1 << 4)    /* Talks only SPI protocols */
#define MMC_CAP_NEEDS_POLL  (1 << 5)    /* Needs polling for card-detection */
#define MMC_CAP_8_BIT_DATA  (1 << 6)    /* Can the host do 8 bit transfers */
#define MMC_CAP_DISABLE     (1 << 7)    /* Can the host be disabled */
#define MMC_CAP_NONREMOVABLE    (1 << 8)    /* Nonremovable e.g. eMMC */
#define MMC_CAP_WAIT_WHILE_BUSY (1 << 9)    /* Waits while card is busy */
#define MMC_CAP_ERASE       (1 << 10)   /* Allow erase/trim commands */
#define MMC_CAP_1_8V_DDR    (1 << 11)   /* can support */
                        /* DDR mode at 1.8V */
#define MMC_CAP_1_2V_DDR    (1 << 12)   /* can support */
                        /* DDR mode at 1.2V */
#define MMC_CAP_POWER_OFF_CARD  (1 << 13)   /* Can power off after boot */
#define MMC_CAP_BUS_WIDTH_TEST  (1 << 14)   /* CMD14/CMD19 bus width ok */
#define MMC_CAP_UHS_SDR12   (1 << 15) /* Host supports UHS SDR12 mode */
#define MMC_CAP_UHS_SDR25   (1 << 16) /* Host supports UHS SDR25 mode */
#define MMC_CAP_UHS_SDR50   (1 << 17) /* Host supports UHS SDR50 mode */
#define MMC_CAP_UHS_SDR104  (1 << 18) /* Host supports UHS SDR104 mode */
#define MMC_CAP_UHS_DDR50   (1 << 19) /* Host supports UHS DDR50 mode */
#define MMC_CAP_SET_XPC_330 (1 << 20) /* Host supports >150mA current at 3.3V */
#define MMC_CAP_SET_XPC_300 (1 << 21) /* Host supports >150mA current at 3.0V */
#define MMC_CAP_SET_XPC_180 (1 << 22) /* Host supports >150mA current at 1.8V */
#define MMC_CAP_DRIVER_TYPE_A   (1 << 23) /* Host supports Driver Type A */
#define MMC_CAP_DRIVER_TYPE_C   (1 << 24) /* Host supports Driver Type C */
#define MMC_CAP_DRIVER_TYPE_D   (1 << 25) /* Host supports Driver Type D */
#define MMC_CAP_MAX_CURRENT_200 (1 << 26) /* Host max current limit is 200mA */
#define MMC_CAP_MAX_CURRENT_400 (1 << 27) /* Host max current limit is 400mA */
#define MMC_CAP_MAX_CURRENT_600 (1 << 28) /* Host max current limit is 600mA */
#define MMC_CAP_MAX_CURRENT_800 (1 << 29) /* Host max current limit is 800mA */
#define MMC_CAP_CMD23       (1 << 30)   /* CMD23 supported. */
#define MMC_CAP_HW_RESET    (1 << 31)   /* Hardware reset */


#define MMC_CAP2_BOOTPART_NOACC (1 << 0)   /* Boot partition no access */
#define MMC_CAP2_CACHE_CTRL (1 << 1)       /* Allow cache control */
#define MMC_CAP2_POWEROFF_NOTIFY (1 << 2)  /* Notify poweroff supported */
#define MMC_CAP2_NO_MULTI_READ  (1 << 3)   /* Multiblock reads don't work */
#define MMC_CAP2_FORCE_MULTIBLOCK (1 << 4) /* Always use multiblock transfers */

#define COMPLETION_DELAY (100000)

#define TYPE_BCM2835_EMMC "bcm2835_emmc"
#define BCM2835_EMMC(obj) \
        OBJECT_CHECK(bcm2835_emmc_state, (obj), TYPE_BCM2835_EMMC)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;

    SDState *card;

    uint32_t arg2;
    uint32_t blksizecnt;
    uint32_t arg1;
    uint32_t cmdtm;
    uint32_t resp0;
    uint32_t resp1;
    uint32_t resp2;
    uint32_t resp3;
    uint32_t data;
    uint32_t status;
    uint32_t control0;
    uint32_t control1;
    uint32_t interrupt;
    uint32_t irpt_mask;
    uint32_t irpt_en;
    uint32_t control2;
    uint32_t force_irpt;
    uint32_t spi_int_spt;
    uint32_t slotisr_ver;
    uint32_t caps;
    uint32_t caps2;
    uint32_t maxcurr;
    uint32_t maxcurr2;

    int acmd;
    int write_op;

    uint32_t bytecnt;

    QEMUTimer *delay_timer;
    qemu_irq irq;

} bcm2835_emmc_state;

static void bcm2835_emmc_set_irq(bcm2835_emmc_state *s)
{
    if (s->status & SDHCI_SPACE_AVAILABLE) {
        s->interrupt |= SDHCI_INT_SPACE_AVAIL;
    }
    if (s->status & SDHCI_DATA_AVAILABLE) {
        s->interrupt |= SDHCI_INT_DATA_AVAIL;
    }
    if (s->irpt_en & s->irpt_mask & s->interrupt) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static void autocmd12(bcm2835_emmc_state *s)
{
    SDRequest request;
    uint8_t response[16];

    if (!(s->cmdtm & SDHCI_TRNS_AUTO_CMD12)) {
        return;
    }

    request.cmd = 12;
    request.arg = 0;
    request.crc = 0;
    sd_do_command(s->card, &request, response);
}

static void autocmd23(bcm2835_emmc_state *s)
{
    SDRequest request;
    uint8_t response[16];

    if (!(s->cmdtm & SDHCI_TRNS_AUTO_CMD23)) {
        return;
    }

    request.cmd = 23;
    request.arg = (s->blksizecnt >> 16) & 0xffff;
    request.crc = 0;
    sd_do_command(s->card, &request, response);
}

static void delayed_completion(void *opaque)
{
    bcm2835_emmc_state *s = (bcm2835_emmc_state *)opaque;

    s->interrupt |= SDHCI_INT_DATA_END;
    autocmd12(s);

    bcm2835_emmc_set_irq(s);
}


static uint64_t bcm2835_emmc_read(void *opaque, hwaddr offset,
    unsigned size)
{
    bcm2835_emmc_state *s = (bcm2835_emmc_state *)opaque;
    uint32_t res = 0;
    uint8_t tmp = 0;
    int set_irq = 0;
    uint32_t blkcnt;
    uint8_t cmd;
    int64_t now;

    assert(size == 4);

    switch (offset) {
    case SDHCI_ARGUMENT2:      /* ARG2 */
        res = s->arg2;
        break;
    case SDHCI_BLOCK_SIZE:     /* BLKSIZECNT */
        res = s->blksizecnt;
        break;
    case SDHCI_ARGUMENT:       /* ARG1 */
        res = s->arg1;
        break;
    case SDHCI_TRANSFER_MODE:   /* CMDTM */
        res = s->cmdtm;
        break;
    case SDHCI_RESPONSE+0:      /* RESP0 */
        res = s->resp0;
        break;
    case SDHCI_RESPONSE+4:      /* RESP1 */
        res = s->resp1;
        break;
    case SDHCI_RESPONSE+8:      /* RESP2 */
        res = s->resp2;
        break;
    case SDHCI_RESPONSE+12:      /* RESP3 */
        res = s->resp3;
        break;
    case SDHCI_BUFFER:          /* DATA */
        cmd = ((s->cmdtm >> (16 + 8)) & 0x3f);

        s->data = 0;
        tmp = sd_read_data(s->card);
        s->data |= (tmp << 0);
        tmp = sd_read_data(s->card);
        s->data |= (tmp << 8);
        tmp = sd_read_data(s->card);
        s->data |= (tmp << 16);
        tmp = sd_read_data(s->card);
        s->data |= (tmp << 24);

        s->status |= SDHCI_DATA_AVAILABLE;

        s->bytecnt += 4;

        if (s->bytecnt == 512) {
            s->bytecnt = 0;
            if (s->cmdtm & SDHCI_TRNS_BLK_CNT_EN) {
                blkcnt = (s->blksizecnt >> 16) & 0xffff;
                blkcnt--;
                s->blksizecnt = (blkcnt << 16) | (s->blksizecnt & 0xffff);
                if (blkcnt == 0) {
                    s->status &= ~SDHCI_DATA_AVAILABLE;

                    if (COMPLETION_DELAY > 0) {
                        now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
                        timer_mod(s->delay_timer,
                            now + COMPLETION_DELAY);
                    } else {
                        s->interrupt |= SDHCI_INT_DATA_END;
                        autocmd12(s);
                    }
                }
            }
            if (!s->acmd && (cmd == 17)) {
                /* Single read */
                s->status &= ~SDHCI_DATA_AVAILABLE;

                s->interrupt |= SDHCI_INT_DATA_END;
            }
        }
        if (!sd_data_ready(s->card)) {
            s->status &= ~SDHCI_DATA_AVAILABLE;

            s->interrupt |= SDHCI_INT_DATA_END;
        }
        set_irq = 1;
        res = s->data;
        break;
    case SDHCI_PRESENT_STATE:   /* STATUS */
        res = s->status;
        break;
    case SDHCI_HOST_CONTROL:    /* CONTROL0 */
        res = s->control0;
        break;
    case SDHCI_CLOCK_CONTROL:   /* CONTROL1 */
        res = s->control1;
        break;
    case SDHCI_INT_STATUS:      /* INTERRUPT */
        res = s->interrupt;
        break;
    case SDHCI_INT_ENABLE:      /* IRPT_MASK */
        res = s->irpt_mask;
        break;
    case SDHCI_SIGNAL_ENABLE:   /* IRPT_EN */
        res = s->irpt_en;
        break;
    case SDHCI_CAPABILITIES:
        res = s->caps;
        break;
    case SDHCI_CAPABILITIES_1:
        res = s->caps;
        break;
    case SDHCI_ACMD12_ERR:      /* CONTROL2 */
        res = s->control2;
        break;
    case SDHCI_SET_ACMD12_ERROR:    /* FORCE_IRPT */
        res = s->force_irpt;
        break;
    case SDHCI_SLOT_INT_STATUS: /* SLOTISR_VERSION */
        res = s->slotisr_ver;
        break;
    case SDHCI_MAX_CURRENT:
        res = s->maxcurr;
        break;
    case SDHCI_MAX_CURRENT+4:
        res = s->maxcurr2;
        break;
    default:
        break;
    }

    if (set_irq) {
        bcm2835_emmc_set_irq(s);
    }

    return res;
}

static void bcm2835_emmc_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    bcm2835_emmc_state *s = (bcm2835_emmc_state *)opaque;
    uint8_t cmd;
    SDRequest request;
    uint8_t response[16];
    int resplen;
    uint32_t blkcnt;
    int64_t now;

    assert(size == 4);

    switch (offset) {
    case SDHCI_ARGUMENT2:      /* ARG2 */
        s->arg2 = value;
        break;
    case SDHCI_BLOCK_SIZE:     /* BLKSIZECNT */
        s->blksizecnt = value;
        break;
    case SDHCI_ARGUMENT:       /* ARG1 */
        s->arg1 = value;
        break;
    case SDHCI_TRANSFER_MODE:   /* CMDTM */
        s->cmdtm = value;
        cmd = ((value >> (16 + 8)) & 0x3f);

        if (!s->acmd && (cmd == 18 || cmd == 25)) {
            autocmd23(s);
        }

        request.cmd = cmd;
        request.arg = s->arg1;
        request.crc = 0;

        s->bytecnt = 0;

        s->status &= ~SDHCI_DATA_AVAILABLE;
        s->status &= ~SDHCI_SPACE_AVAILABLE;

        resplen = sd_do_command(s->card, &request, response);

        if (resplen > 0) {
            if (resplen == 4) {
                s->resp0 = (response[0] << 24)
                    | (response[1] << 16)
                    | (response[2] << 8)
                    | (response[3] << 0);
                if (!s->acmd && ((cmd == 24) || (cmd == 25))) {
                    s->status |= SDHCI_SPACE_AVAILABLE;
                }
            } else if (resplen == 16) {
                s->resp3 = 0
                    | (response[1-1] << 16)
                    | (response[2-1] << 8)
                    | (response[3-1] << 0);
                s->resp2 = 0
                    | (response[0+4-1] << 24)
                    | (response[1+4-1] << 16)
                    | (response[2+4-1] << 8)
                    | (response[3+4-1] << 0);
                s->resp1 = 0
                    | (response[0+8-1] << 24)
                    | (response[1+8-1] << 16)
                    | (response[2+8-1] << 8)
                    | (response[3+8-1] << 0);
                s->resp0 = 0
                    | (response[0+12-1] << 24)
                    | (response[1+12-1] << 16)
                    | (response[2+12-1] << 8)
                    | (response[3+12-1] << 0);
            }

            s->interrupt |= SDHCI_INT_RESPONSE;

            if (!s->acmd && (cmd == 12)) {
                /* Stop transmission */
                s->status &= ~SDHCI_SPACE_AVAILABLE;
                s->interrupt |= SDHCI_INT_DATA_END;
            } else {
                if (sd_data_ready(s->card)) {
                    s->status |= SDHCI_DATA_AVAILABLE;
                }
            }
            bcm2835_emmc_set_irq(s);
        } else {
            /* Unrecognized commands */
            if ((!s->acmd && (cmd == 52))
                || (!s->acmd && (cmd == 5))
       ) {
                s->interrupt |= SDHCI_INT_TIMEOUT;
                s->interrupt |= SDHCI_INT_ERROR;
            }
            if (!s->acmd && (cmd == 0)) {
                s->interrupt |= SDHCI_INT_RESPONSE;
            }
            if (!s->acmd && (cmd == 7)) {
                s->interrupt |= SDHCI_INT_RESPONSE;
            }
            bcm2835_emmc_set_irq(s);
        }
        if (cmd == 55) {
            s->acmd = 1;
        } else {
            s->acmd = 0;
        }
        break;
    case SDHCI_BUFFER:          /* DATA */
        cmd = ((s->cmdtm >> (16 + 8)) & 0x3f);

        s->data = value;

        sd_write_data(s->card, (value >> 0) & 0xff);
        sd_write_data(s->card, (value >> 8) & 0xff);
        sd_write_data(s->card, (value >> 16) & 0xff);
        sd_write_data(s->card, (value >> 24) & 0xff);

        s->status |= SDHCI_SPACE_AVAILABLE;

        s->bytecnt += 4;

        if (s->bytecnt == 512) {
            s->bytecnt = 0;
            if (s->cmdtm & SDHCI_TRNS_BLK_CNT_EN) {
                blkcnt = (s->blksizecnt >> 16) & 0xffff;
                blkcnt--;
                s->blksizecnt = (blkcnt << 16) | (s->blksizecnt & 0xffff);
                if (blkcnt == 0) {
                    if (COMPLETION_DELAY > 0) {
                        now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
                        timer_mod(s->delay_timer,
                            now + COMPLETION_DELAY);
                    } else {
                        s->interrupt |= SDHCI_INT_DATA_END;
                        autocmd12(s);
                    }
                }
            }
            if (!s->acmd && (cmd == 24)) {
                /* Single write */
                s->status &= ~SDHCI_SPACE_AVAILABLE;

                s->interrupt |= SDHCI_INT_DATA_END;
            }
        }
        bcm2835_emmc_set_irq(s);
        break;
    case SDHCI_HOST_CONTROL:    /* CONTROL0 */
        s->control0 &= ~0x007f0026;
        value &= 0x007f0026;
        s->control0 |= value;
        break;
    case SDHCI_CLOCK_CONTROL:  /* CONTROL1 */
        s->control0 &= ~0x070fffe7;
        value &= 0x070fffe7;
        if (value & ((SDHCI_RESET_ALL
            | SDHCI_RESET_CMD
            | SDHCI_RESET_DATA) << 24)) {
            /* Reset */
            value &= ~((SDHCI_RESET_ALL
                | SDHCI_RESET_CMD
                | SDHCI_RESET_DATA) << 24);
        }
        s->control1 |= value;
        break;
    case SDHCI_INT_STATUS:      /* INTERRUPT */
        s->interrupt &= ~value;
        bcm2835_emmc_set_irq(s);
        break;

    case SDHCI_INT_ENABLE:      /* IRPT_MASK */
        s->irpt_mask = value;
        break;
    case SDHCI_SIGNAL_ENABLE:   /* IRPT_EN */
        s->irpt_en = value;
        break;
    case SDHCI_ACMD12_ERR:      /* CONTROL2 */
        s->control2 &= ~0x00e7009f;
        value &= 0x00e7009f;
        s->control2 |= value;
        break;
    case SDHCI_SET_ACMD12_ERROR:    /* FORCE_IRPT */
        s->force_irpt = value;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps bcm2835_emmc_ops = {
    .read = bcm2835_emmc_read,
    .write = bcm2835_emmc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_emmc = {
    .name = TYPE_BCM2835_EMMC,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int bcm2835_emmc_init(SysBusDevice *sbd)
{
    DriveInfo *di;
    DeviceState *dev = DEVICE(sbd);
    bcm2835_emmc_state *s = BCM2835_EMMC(dev);

    di = drive_get(IF_SD, 0, 0);
    if (!di) {
        fprintf(stderr, "bcm2835_emmc: missing SD card\n");
        exit(1);
    }
    s->card = sd_init(di->bdrv, 0);

    s->arg2 = 0;
    s->blksizecnt = 0;
    s->arg1 = 0;
    s->cmdtm = 0;
    s->resp0 = 0;
    s->resp1 = 0;
    s->resp2 = 0;
    s->resp3 = 0;
    s->data = 0;
    s->status = (0x1ff << 16);
    s->control0 = 0;
    s->control1 = SDHCI_CLOCK_INT_STABLE;
    s->interrupt = 0;
    s->irpt_mask = 0;
    s->irpt_en = 0;
    s->control2 = 0;
    s->force_irpt = 0;
    s->spi_int_spt = 0;
    s->slotisr_ver = (0x9900 | SDHCI_SPEC_300) << 16;
    s->caps = 0;
    s->caps2 = 0;
    s->maxcurr = 1;
    s->maxcurr2 = 0;

    s->acmd = 0;
    s->write_op = 0;

    s->delay_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, delayed_completion, s);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_emmc_ops, s,
        TYPE_BCM2835_EMMC, 0x100000);
    sysbus_init_mmio(sbd, &s->iomem);
    vmstate_register(dev, -1, &vmstate_bcm2835_emmc, s);

    sysbus_init_irq(sbd, &s->irq);

    return 0;
}

static void bcm2835_emmc_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = bcm2835_emmc_init;
}

static TypeInfo bcm2835_emmc_info = {
    .name          = TYPE_BCM2835_EMMC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2835_emmc_state),
    .class_init    = bcm2835_emmc_class_init,
};

static void bcm2835_emmc_register_types(void)
{
    type_register_static(&bcm2835_emmc_info);
}

type_init(bcm2835_emmc_register_types)
