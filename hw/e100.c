/*
 * QEMU E100(i82557) ethernet card emulation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Copyright (c) 2006-2007 Stefan Weil
 * Copyright (c) 2006-2007 Zhang Xin(xing.z.zhang@intel.com)
 *
 * Support OS:
 *      x86 linux and windows
 *      PAE linux and windows
 *      x86_64 linux and windows
 *      IA64 linux and windows
 *
 * Untested:
 *      Big-endian machine
 *
 * References:
 *
 * Intel 8255x 10/100 Mbps Ethernet Controller Family
 * Open Source Software Developer Manual
 */

#include <assert.h>

#include "hw.h"
#include "pci.h"
#include "net.h"
#include "qemu_socket.h"

enum
{
    //~ E100_PCI_VENDOR_ID = 0x00,        /* 16 bits */
    //~ E100_PCI_DEVICE_ID = 0x02,        /* 16 bits */
    //~ E100_PCI_COMMAND = 0x04,        /* 16 bits */
    //~ E100_PCI_STATUS = 0x06,            /* 16 bits */
    E100_PCI_REVISION_ID = 0x08,    /* 8 bits */
    //~ E100_PCI_CLASS_CODE = 0x0b,        /* 8 bits */
    //~ E100_PCI_SUBCLASS_CODE = 0x0a,    /* 8 bits */
    //~ E100_PCI_HEADER_TYPE = 0x0e,    /* 8 bits */
    //~ E100_PCI_BASE_ADDRESS_0 = 0x10,    /* 32 bits */
    //~ E100_PCI_BASE_ADDRESS_1 = 0x14,    /* 32 bits */
    //~ E100_PCI_BASE_ADDRESS_2 = 0x18,    /* 32 bits */
    //~ E100_PCI_BASE_ADDRESS_3 = 0x1c,    /* 32 bits */
    //~ E100_PCI_BASE_ADDRESS_4 = 0x20,    /* 32 bits */
    //~ E100_PCI_BASE_ADDRESS_5 = 0x24    /* 32 bits */
}PCI_CONFIGURE_SPACE;

#define PCI_CONFIG_8(offset, value) \
    (*(uint8_t *)&pci_conf[offset] = (value))
#define PCI_CONFIG_16(offset, value) \
    (*(uint16_t *)&pci_conf[offset] = cpu_to_le16(value))
#define PCI_CONFIG_32(offset, value) \
    (*(uint32_t *)&pci_conf[offset] = cpu_to_le32(value))

// Alias for Control/Status register read/write
#define CSR_STATUS  scb_status
#define CSR_CMD scb_cmd
#define CSR_POINTER scb_pointer
#define CSR_PORT port
#define CSR_EEPROM eeprom_ctrl
#define CSR_MDI mdi_ctrl
#define CSR_PM pm_reg

#define CSR(class, field)   \
    (s->pci_mem.csr.class.u.field)
#define CSR_VAL(class)  \
    (s->pci_mem.csr.class.val)

#define CSR_READ(x, type)    \
    ({  \
        type t; \
        memcpy(&t, &s->pci_mem.mem[x], sizeof(type)); \
        t;  \
     })

#define CSR_WRITE(x, val, type)    \
    ({  \
        type t = val; \
        memcpy(&s->pci_mem.mem[x], &t, sizeof(type)); \
     })

#define SET_CU_STATE(val)    \
    (CSR(CSR_STATUS, cus) = val)
#define GET_CU_STATE    \
    (CSR(CSR_STATUS, cus))

#define SET_RU_STATE(val)    \
    (CSR(CSR_STATUS, rus) = val)
#define GET_RU_STATE    \
    (CSR(CSR_STATUS, rus))

#define KiB 1024

#define EEPROM_SIZE     64

#define BIT(n) (1U << (n))
#define USE_BUFFER_TCP
/* debug E100 card */
//#define DEBUG_E100


#ifdef DEBUG_E100
#define logout(fmt, args...) fprintf(stderr, "EE100\t%-28s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

#define MAX_ETH_FRAME_SIZE 1514

/* This driver supports several different devices which are declared here. */
#define i82551          0x82551
#define i82557B         0x82557b
#define i82557C         0x82557c
#define i82558B         0x82558b
#define i82559C         0x82559c
#define i82559ER        0x82559e
#define i82562          0x82562

#define PCI_MEM_SIZE            (4 * KiB)
#define PCI_IO_SIZE             (64)
#define PCI_FLASH_SIZE          (128 * KiB)

enum
{
    OP_READ,
    OP_WRITE,
} OPERATION_DIRECTION;

/* The SCB accepts the following controls for the Tx and Rx units: */
enum
{
    CU_NOP = 0x0000,        /* No operation */
    CU_START = 0x0010,        /* CU start     */
    CU_RESUME = 0x0020,        /* CU resume    */
    CU_STATSADDR = 0x0040,    /* Load dump counters address */
    CU_SHOWSTATS = 0x0050,    /* Dump statistical counters */
    CU_CMD_BASE = 0x0060,    /* Load CU base address */
    CU_DUMPSTATS = 0x0070,    /* Dump and reset statistical counters */
    CU_S_RESUME = 0x00a0    /* CU static resume */
}CONTROL_UNIT_COMMAND;

enum
{
    RU_NOP = 0x0000,
    RU_START = 0x0001,
    RU_RESUME = 0x0002,
    RU_DMA_REDIRECT = 0x0003,
    RU_ABORT = 0x0004,
    RU_LOAD_HDS = 0x0005,
    RU_ADDR_LOAD = 0x0006,
    RU_RESUMENR = 0x0007,
}RECEIVE_UNIT_COMMAND;

/* SCB status word descriptions */
enum
{
    CU_IDLE = 0,
    CU_SUSPENDED = 1,
    CU_LPQ_ACTIVE = 2,
    CU_HQP_ACTIVE = 3
} CONTROL_UINT_STATE;

enum
{
    RU_IDLE = 0,
    RU_SUSPENDED = 1,
    RU_NO_RESOURCES =2,
    RU_READY = 4
} RECEIVE_UNIT_STATE;

enum
{
    PORT_SOFTWARE_RESET = 0,
    PORT_SELF_TEST = 1,
    PORT_SELECTIVE_RESET = 2,
    PORT_DUMP = 3,
    PORT_DUMP_WAKE_UP = 7,
}SCB_PORT_SELECTION_FUNCTION;

enum
{
    CBL_NOP = 0,
    CBL_IASETUP = 1,
    CBL_CONFIGURE = 2,
    CBL_MULTCAST_ADDR_SETUP = 3,
    CBL_TRANSMIT = 4,
    CBL_LOAD_MICROCODE = 5,
    CBL_DUMP = 6,
    CBL_DIAGNOSE = 7,
}CBL_COMMAND;

enum
{
    SCB_STATUS = 0,            /* SCB base + 0x00h, RU states + CU states + STAT/ACK */
    SCB_ACK = 1,            /* SCB ack/stat */
    SCB_CMD = 2,            /* RU command + CU command + S bit + M bit */
    SCB_INTERRUPT_MASK = 3, /* Interrupts mask bits */
    SCB_POINTER = 4,        /* SCB general pointer, depending on command type */
    SCB_PORT = 8,            /* SCB port register */
    SCB_EEPROM = 0xe,        /* SCB eeprom control register */
    SCB_MDI =0x10,            /* SCB MDI control register */
} CSR_OFFSETS;

enum
{
    EEPROM_SK = 0x01,
    EEPROM_CS = 0x02,
    EEPROM_DI = 0x04,
    EEPROM_DO = 0x08,
} EEPROM_CONTROL_REGISTER;

enum
{
    EEPROM_READ = 0x2,
    EEPROM_WRITE = 0x1,
    EEPROM_ERASE = 0x3,
} EEPROM_OPCODE;

enum
{
    MDI_WRITE = 0x1,
    MDI_READ = 0x2,
} MDI_OPCODE;

enum
{
    INT_FCP = BIT(8),
    INT_SWI = BIT(10),
    INT_MDI = BIT(11),
    INT_RNR = BIT(12),
    INT_CNA = BIT(13),
    INT_FR = BIT(14),
    INT_CX_TNO = BIT(15),
} E100_INTERRUPT;

enum
{
    CSR_MEMORY_BASE,
    CSR_IO_BASE,
    FLASH_MEMORY_BASE,
    REGION_NUM
}E100_PCI_MEMORY_REGION;

typedef struct {
    uint32_t tx_good_frames,        // Good frames transmitted
             tx_max_collisions,     // Fatal frames -- had max collisions
             tx_late_collisions,    // Fatal frames -- had a late coll.
             tx_underruns,          // Transmit underruns (fatal or re-transmit)
             tx_lost_crs,           // Frames transmitted without CRS
             tx_deferred,           // Deferred transmits
             tx_single_collisions,  // Transmits that had 1 and only 1 coll.
             tx_multiple_collisions,// Transmits that had multiple coll.
             tx_total_collisions,   // Transmits that had 1+ collisions.

             rx_good_frames,        // Good frames received
             rx_crc_errors,         // Aligned frames that had a CRC error
             rx_alignment_errors,   // Receives that had alignment errors
             rx_resource_errors,    // Good frame dropped due to lack of resources
             rx_overrun_errors,     // Overrun errors - bus was busy
             rx_cdt_errors,         // Received frames that encountered coll.
             rx_short_frame_errors, // Received frames that were to short

             complete_word;         // A005h indicates dump cmd completion,
                                    // A007h indicates dump and reset cmd completion.

// TODO: Add specific field for i82558, i82559
} __attribute__ ((packed)) e100_stats_t;

#define EEPROM_I82557_ADDRBIT 6
/* Below data is dumped from a real I82557 card */
static const uint16_t eeprom_i82557[] =
{
    0x300, 0xe147, 0x2fa4, 0x203, 0x0, 0x201, 0x4701, 0x0, 0x7414, 0x6207,
    0x4082, 0xb, 0x8086, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x128, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc374,
};

static const uint8_t e100_pci_configure[] =
{
    0x86, 0x80, 0x29, 0x12, 0x17, 0x00, 0x90, 0x02, 0x08, 0x00, 0x00, 0x02, 0x10, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x50, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x80, 0x0b, 0x00,
    0x00, 0x00, 0xf0, 0xff, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x01, 0x08, 0x38,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x22, 0xfe,
    0x00, 0x40, 0x00, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

typedef struct
{
#define OPCODE      0xb
#define ADDR        0xc
#define DATA        0xd
#define NOP         0xe

#define EEPROM_RESET_ALL      0xfe
#define EEPROM_SELECT_RESET   0xff
    uint8_t  start_bit;
    uint8_t  opcode;
    uint8_t  address;
    uint16_t data;  //This must be 16 bit represents a register in eeprom

    uint32_t val;
    uint32_t val_len;
    uint8_t  val_type;  // What data type is in DI. opcode?address?data?

    uint8_t cs;
    uint8_t sk;

    // This two fileds only be reset when device init
    uint16_t addr_len;
    uint16_t contents[256]; // 256 is enough to all device(i82557 ... i82559)
} eeprom_t;

// Control/Status register structure
typedef struct
{
    /* SCB status word */
    union
    {
        uint16_t val;
        struct
        {
            uint8_t rs1:2;  // Reserved
            uint8_t rus:4;  // RU status
            uint8_t cus:2;  // CU status
            uint8_t stat_ack; // Stat/ACK
        }u;
    }scb_status;

    /* SCB command word */
    union
    {
        uint16_t val;
        struct
        {
            uint8_t ru_cmd:3;   // RU command
            uint8_t rs1:1;      // Reserved
            uint8_t cu_cmd:4;   // CU command
            uint8_t m:1;        // Interrup mask bit(1:mask all interrupt)
            uint8_t si:1;       // Use for software cause interrupt
            uint8_t simb:6;     // Specific interrupt mask bit
        }u;
    }scb_cmd;

    /* SCB general pointer */
    union
    {
        uint32_t val;
        struct
        {
            uint32_t scb_ptr;
        }u;
    }scb_pointer;

    /* Port interface */
    union
    {
        uint32_t val;
        struct
        {
            uint8_t opcode:4;   // Op code for function selection
            uint32_t ptr:28;    // Result pointer
        }u;
    }port;

    uint16_t rs1;               // Reserved

    /* EEPROM control register */
    union
    {
        uint16_t val;
        struct
        {
            uint8_t eesk:1;      // Serial clock
            uint8_t eecs:1;      // Chip select
            uint8_t eedi:1;      // Serial data in
            uint8_t eedo:1;      // Serial data out
            uint8_t rs1:4;       // Reserved
            uint8_t data;
        }u;
    }eeprom_ctrl;

    /* MDI control register */
    union
    {
        uint32_t val;
        struct
        {
            uint16_t data;       // Data
            uint8_t regaddr:5;   // PHY register address
            uint8_t phyaddr:5;   // PHY address
            uint8_t opcode:2;    // Opcode
            uint8_t r:1;         // Ready
            uint8_t ie:1;        // Interrup enable
            uint8_t rs1:2;       // Reserved
        }u;
    } mdi_ctrl;

    /* Receive byte counter register */
    uint32_t rx_byte_counter;

    /* Early receive interrupt register */
    uint8_t early_interrupt;

    /* Flow control register */
    union
    {
        uint16_t val;
    }flow_ctrl;

    /* Power management driver register */
    union
    {
        uint8_t val;
        struct
        {
            uint8_t pme_s:1;     // PME status
            uint8_t tco_r:1;     // TCO request
            uint8_t f_tco_i:1;   // Force TCO indication
            uint8_t tco_re:1;    // TCO ready
            uint8_t rs1:1;       // Reserved
            uint8_t isp:1;       // Intersting packet
            uint8_t mg:1;        // Magic packet
            uint8_t lsci:1;      // Link status change indication
        }u;
    }pm_reg;

    /* General control register */
    uint8_t gen_ctrl;

    /* General status register */
    uint8_t gen_status;

    /* These are reserved or we don't support register */
    uint8_t others[30];
} __attribute__ ((packed)) csr_t;

typedef struct
{
    uint8_t byte_count;
    uint8_t rx_fifo_limit:4;
    uint8_t tx_fifo_limit:4;
    uint8_t adpt_inf_spacing;
    uint8_t rs1;
    uint8_t rx_dma_max_bytes;
    uint8_t tx_dma_max_bytes:7;
    uint8_t dmbc_en:1;
    uint8_t late_scb:1,
            rs2:1,
            tno_intr:1,
            ci_intr:1,
            rs3:1,
            rs4:1,
            dis_overrun_rx:1,
            save_bad_frame:1;
    uint8_t dis_short_rx:1,
            underrun_retry:2,
            rs5:5;
    uint8_t mii:1,
            rs6:7;
    uint8_t rs7;
    uint8_t rs8:3,
            nsai:1,
            preamble_len:2,
            loopback:2;
    uint8_t linear_prio:3,
            rs9:5;
    uint8_t pri_mode:1,
            rs10:3,
            interframe_spacing:4;
    uint16_t rs11;
    uint8_t promiscuous:1,
            broadcast_dis:1,
            rs12:5,
            crs_cdt:1;
    uint16_t rs13;
    uint8_t strip:1,
            padding:1,
            rx_crc:1,
            rs14:5;
    uint8_t rs15:6,
            force_fdx:1,
            fdx_en:1;
    uint8_t rs16:6,
            mul_ia:2;
    uint8_t rs17:3,
            mul_all:1,
            rs18:4;
} __attribute__ ((packed)) i82557_cfg_t;

typedef struct {
    VLANClientState *vc;
    PCIDevice *pci_dev;
    int mmio_index;
    uint8_t scb_stat;           /* SCB stat/ack byte */
    uint32_t region_base_addr[REGION_NUM];         /* PCI region addresses */
    uint8_t macaddr[6];
    uint16_t mdimem[32];
    eeprom_t eeprom;
    uint32_t device;            /* device variant */

    uint8_t mult_list[8];       /* Multicast address list */
    int is_multcast_enable;

    /* (cu_base + cu_offset) address the next command block in the command block list. */
    uint32_t cu_base;           /* CU base address */
    uint32_t cu_offset;         /* CU address offset */
    uint32_t cu_next;           /* Point to next command when CU go to suspend */

    /* (ru_base + ru_offset) address the RFD in the Receive Frame Area. */
    uint32_t ru_base;           /* RU base address */
    uint32_t ru_offset;         /* RU address offset */

    uint32_t statsaddr;         /* pointer to e100_stats_t */

    e100_stats_t statistics;        /* statistical counters */

    /* Configuration bytes. */
    i82557_cfg_t config;

    /* FIFO buffer of card. The packet that need to be sent buffered in it */
    uint8_t pkt_buf[MAX_ETH_FRAME_SIZE+4];
    /* Data length in FIFO buffer */
    int pkt_buf_len;
#ifdef USE_BUFFER_TCP
    int buffer_tcp_enable;
    int continuous_tcp_frame;
    int unflush_tcp_num;
#endif

    /* Data in mem is always in the byte order of the controller (le). */
    union
    {
        csr_t csr;
        uint8_t mem[PCI_MEM_SIZE];
    }pci_mem;

} E100State;

/* CB structure, filled by device driver
 * This is a common structure of CB. In some
 * special case such as TRANSMIT command, the
 * reserved field will be used.
 */
struct  control_block
{
    uint16_t rs1:13;            /* reserved */
    uint8_t ok:1;               /* 1:command executed without error, otherwise 0 */
    uint8_t rs2:1;
    uint8_t c:1;                /* execution status. set by device, clean by software */
    uint8_t cmd:3;              /* command */
    uint16_t rs3:10;            /* most time equal to 0 */
    uint8_t i:1;                /* whether trigger interrupt after execution. 1:yes; 0:no */
    uint8_t s:1;                /* suspend */
    uint8_t el:1;               /* end flag */
    uint32_t link_addr;
} __attribute__ ((packed));

typedef struct
{
    uint32_t tx_desc_addr;      /* transmit buffer decsriptor array address. */
    uint16_t tcb_bytes:14;         /* transmit command block byte count (in lower 14 bits)*/
    uint8_t rs1:1;
    uint8_t eof:1;
    uint8_t tx_threshold;       /* transmit threshold */
    uint8_t tbd_num;          /* TBD number */
} __attribute__ ((packed)) tbd_t;

/* Receive frame descriptore structure */
typedef struct
{
    uint16_t status:13;     // Result of receive opration
    uint8_t ok:1;           // 1:receive without error, otherwise 0
    uint8_t rs1:1;
    uint8_t c:1;            // 1:receive complete
    uint8_t rs2:3;
    uint8_t sf:1;           // 0:simplified mode
    uint8_t h:1;            // 1:header RFD
    uint16_t rs3:9;
    uint8_t s:1;            // 1:go to suspend
    uint8_t el:1;           // 1:last RFD
    uint32_t link_addr;     // Add on RU base point to next RFD
    uint32_t rs4;
    uint16_t count:14;      // Number of bytes written into data area
    uint8_t f:1;            // Set by device when count field update
    uint8_t eof:1;          // Set by device when placing data into data area complete
    uint16_t size:14;       // Buffer size (even number)
    uint8_t rs5:2;
} __attribute__ ((packed)) rfd_t;

enum
{
    RX_COLLISION = BIT(0),  // 1:Receive collision detected
    RX_IA_MATCH = BIT(1),      // 0:Receive frame match individual address
    RX_NO_MATCH = BIT(2), // 1:Receive frame match no address
    RX_ERR = BIT(4),        // 1:Receive frame error
    RX_TYPE = BIT(5),       // 1:Receive frame is a type frame
    RX_SHORT = BIT(7),      // 1:Receive frame is too short
    RX_DMA_ERR = BIT(8),
    RX_LARGE = BIT(9),      // 1:Receive frame is too large
    RX_CRC_ERR = BIT(10),
} RFD_STATUS;

typedef struct PCIE100State {
    PCIDevice dev;
    E100State e100;
} PCIE100State;

/* Default values for MDI (PHY) registers */
static const uint16_t e100_mdi_default[] = {
    /* MDI Registers 0 - 6, 7 */
    0x3000, 0x780d, 0x02a8, 0x0154, 0x05e1, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 8 - 15 */
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    /* MDI Registers 16 - 31 */
    0x0003, 0x0000, 0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const uint8_t broadcast_macaddr[6] =
    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* Debugging codes */
#ifdef  DEBUG_E100

static void e100_dump(const char *comment, const uint8_t *info, int len)
{
    int i;

    if ( !comment || !info )
        return;

    fprintf(stderr, "EE100\t%-24s%s", __func__, comment);
    for ( i=0; i<len; i++ )
        fprintf(stderr, "%x ", info[i]);

    fprintf(stderr, "\n");
}

static const char *regname[] =
{
    [0] = "SCB Status", [1] = "SCB Ack",
    [2] = "SCB Cmd", [3] = "SCB Interrupt Mask",
    [4] = "SCB Pointer", [8] = "SCB Port",
    [0xc] = "SCB Flash", [0xe] = "SCB Eeprom",
    [0x10] = "SCB Ctrl MDI", [0x14] = "SCB Early RX",
};
#define SCBNAME(x)    \
    ( (x) < (sizeof(regname) / sizeof(regname[0])) ? regname[(x)] : "Unknown SCB Register" )

static const char *cb_cmd_name[] =
{
    [CBL_NOP] = "NOP", [CBL_IASETUP] = "Individual address setup",
    [CBL_CONFIGURE] = "Configure", [CBL_MULTCAST_ADDR_SETUP] = "Set Multcast address list",
    [CBL_TRANSMIT] = "Transmit", [CBL_LOAD_MICROCODE] = "Load microcode",
    [CBL_DUMP] = "Dump", [CBL_DIAGNOSE] = "Diagnose",
};
#define CB_CMD_NAME(x)  \
    ( (x) < (sizeof(cb_cmd_name) / sizeof(cb_cmd_name[0])) ? cb_cmd_name[(x)] : "Unknown CB command" )

static const char *eeprom_opcode_name[] =
{
    [0] = "Unknow", [EEPROM_WRITE] = "Write",
    [EEPROM_READ] = "Read", [EEPROM_ERASE] = "Erase",
};
#define EEPROM_OPCODE_NAME(x)   \
    ( (x) < (sizeof(eeprom_opcode_name) / sizeof(eeprom_opcode_name[0])) ?  \
      eeprom_opcode_name[(x)] : "Unknown" )

static struct eeprom_trace_data
{
    uint8_t eedo[256];
    uint8_t di[256];
    int op;
    int i;
    uint32_t data;
}etd = {.op = NOP};

static void eeprom_trace(int eedo, int di, int dir, int next_op, int clr)
{
    int i;

    if ( clr )
    {
        const char *opname = NULL;

        switch ( etd.op )
        {
            case NOP:
                break;
            case OPCODE:
                opname = "opcode";
                break;
            case ADDR:
                opname = "address";
                break;
            case DATA:
                opname = "data transfer";
                break;
            default:
                opname = "Unknown";
        }

        if ( opname )
        {
            logout("EEPROM trace:\n");
            fprintf(stderr, "\toperation: %s\n", opname);
            fprintf(stderr, "\tDI track:");
            for ( i=0; i<etd.i; i++ )
                fprintf(stderr, "%x ", etd.di[i]);
            fprintf(stderr, "\n\tDO track:");
            for ( i=0; i<etd.i; i++ )
                fprintf(stderr, "%x ", etd.eedo[i]);
            fprintf(stderr, "\n\tData:%#x\n", etd.data);
        }


        memset(&etd, 0x0, sizeof(etd));
        etd.op = next_op;

        return;
    }

    etd.eedo[etd.i] = eedo;
    etd.di[etd.i] = di;
    etd.i ++;
    if ( dir == EEPROM_READ && etd.op == DATA )
        etd.data = (etd.data << 1) | eedo;
    else
        etd.data = (etd.data << 1) | di;
}

#define INT_NAME(x) \
    ({  \
     const char *name = NULL; \
     switch (x) \
     {  \
     case INT_FCP:  \
            name = "FCP";   \
            break;  \
     case INT_SWI:  \
            name = "SWI";   \
            break;  \
     case INT_MDI:  \
            name = "MDI";   \
            break;  \
     case INT_RNR:  \
            name = "RNR";   \
            break;  \
     case INT_CNA:  \
            name = "CNA";   \
            break;  \
     case INT_FR:   \
            name = "FR";    \
            break;  \
     case INT_CX_TNO:   \
            name ="CX/TNO"; \
            break;  \
     default:   \
            name ="Unknown"; \
     }  \
     name;  \
     })

#else
static void e100_dump(const char *comment, const uint8_t *info, int len) {}
static void eeprom_trace(int eedo, int di, int dir, int next_op, int clr) {}
#endif

static void pci_reset(E100State * s)
{
    uint8_t *pci_conf = s->pci_dev->config;

    memcpy(pci_conf, &e100_pci_configure[0], sizeof(e100_pci_configure));
    logout("%p\n", s);

    /* I82557 */
    PCI_CONFIG_8(E100_PCI_REVISION_ID, 0x01);

    PCI_CONFIG_8(0x3c, 0x0);

}

static void e100_selective_reset(E100State * s)
{

    memset(s->pci_mem.mem, 0x0, sizeof(s->pci_mem.mem));
    // Set RU/CU to idle, maintain the register mentioned in spec,
    SET_CU_STATE(CU_IDLE);
    SET_RU_STATE(RU_IDLE);
    logout("CU and RU go to idle\n");

    s->ru_offset = 0;
    s->cu_offset = 0;
    s->cu_next = 0;

    // For 82557, special interrupt bits are all 1
    CSR(CSR_CMD, simb) = 0x3f;
    // Set PHY to 1
    CSR_VAL(CSR_MDI) |= BIT(21);

    /* Initialize EEDO bit to 1. Due to driver would detect dummy 0 at
     * EEDO bit, so initialize it to 1 is safety a way.
     */
    CSR(CSR_EEPROM, eedo) = 1;
    // no pending interrupts
    s->scb_stat = 0;

    return;
}

static void e100_software_reset(E100State *s)
{
    memset(s->pci_mem.mem, 0x0, sizeof(s->pci_mem.mem));
    // Clear multicast list
    memset(s->mult_list, 0x0, sizeof(s->mult_list));
    // Set MDI register to default value
    memcpy(&s->mdimem[0], &e100_mdi_default[0], sizeof(s->mdimem));
    s->is_multcast_enable = 1;
    /* Clean FIFO buffer */
    memset(s->pkt_buf, 0x0, sizeof(s->pkt_buf));
    s->pkt_buf_len = 0;

    memset(&s->statistics, 0x0, sizeof(s->statistics));
    e100_selective_reset(s);
    return;
}

static void e100_reset(void *opaque)
{
    E100State *s = (E100State *) opaque;
    logout("%p\n", s);
    e100_software_reset(s);
}


static void e100_save(QEMUFile * f, void *opaque)
{
    //TODO
    return;
}

static int e100_load(QEMUFile * f, void *opaque, int version_id)
{
    //TODO
    return 0;
}

/* Interrupt functions */
static void e100_interrupt(E100State *s, uint16_t int_type)
{

    //TODO: Add another i8255x card supported mask bit
    if ( !CSR(CSR_CMD,m) )
    {
        //Set bit in stat/ack, so driver can no what interrupt happen
        CSR_VAL(CSR_STATUS) |= int_type;
        s->scb_stat = CSR(CSR_STATUS, stat_ack);

        /* SCB maske and SCB Bit M do not disable interrupt. */
        logout("Trigger an interrupt(type = %s(%#x), SCB Status = %#x)\n",
                INT_NAME(int_type), int_type, CSR_VAL(CSR_STATUS));
        qemu_irq_raise(s->pci_dev->irq[0]);
    }
}

static void e100_interrupt_ack(E100State * s, uint8_t ack)
{

    /* Ignore acknowledege if driver write 0 to ack or
     * according interrupt bit is not set
     */
    if ( !ack || !(s->scb_stat & ack) )
    {
        logout("Illegal interrupt ack(ack=%#x, SCB Stat/Ack=%#x), ignore it\n",
                ack, s->scb_stat);
        // Due to we do write operation before e100_execute(), so
        // we must restore value of ack field here
        CSR(CSR_STATUS, stat_ack) = s->scb_stat;
        return;
    }

    s->scb_stat &= ~ack;
    CSR(CSR_STATUS, stat_ack) = s->scb_stat;

    logout("Interrupt ack(name=%s,val=%#x)\n", INT_NAME(({uint16_t bit = ack<<8;bit;})),ack);
    if ( !s->scb_stat )
    {
        logout("All interrupts are acknowledeged, de-assert interrupt line\n");
        qemu_irq_lower(s->pci_dev->irq[0]);
    }
}

static void e100_self_test(uint32_t res_addr)
{
    struct
    {
        uint32_t st_sign;           /* Self Test Signature */
        uint32_t st_result;         /* Self Test Results */
    } test_res;

    test_res.st_sign = (uint32_t)-1;
    test_res.st_result = 0; // Our self test always success
    cpu_physical_memory_write(res_addr, (uint8_t *)&test_res, sizeof(test_res));

    logout("Write self test result to %#x\n", res_addr);
}

static void scb_port_func(E100State *s, uint32_t val, int dir)
{
#define PORT_SELECTION_MASK 0xfU

    uint32_t sel = val & PORT_SELECTION_MASK;

    switch ( sel )
    {
        case PORT_SOFTWARE_RESET:
            logout("do PORT_SOFTWARE_RESET!\n");
            e100_software_reset(s);
            break;
        case PORT_SELF_TEST:
            e100_self_test(val & ~PORT_SELECTION_MASK);
            logout("do PORT_SELF_TEST!\n");
            break;
        case PORT_SELECTIVE_RESET:
            logout("do PORT_SELECTIVE_RESET!\n");
            e100_selective_reset(s);
            break;
        case PORT_DUMP:
            logout("do PORT_SOFTWARE_RESET!\n");
            break;
        case PORT_DUMP_WAKE_UP:
            logout("do PORT_SOFTWARE_RESET!\n");
            break;
        default:
            logout("Unkonw SCB port command(selection function = %#x)\n", sel);
    }
}

static void e100_write_mdi(E100State *s, uint32_t val)
{
    uint32_t ie = (val & 0x20000000) >> 29;
    uint32_t opcode = (val & 0x0c000000) >> 26;
    uint32_t phyaddr = (val & 0x03e00000) >> 21;
    uint32_t regaddr = (val & 0x001f0000) >> 16;
    uint32_t data = val & 0x0000ffff;

    logout("Write MDI:\n"
           "\topcode:%#x\n"
           "\tphy address:%#x\n"
           "\treg address:%#x\n"
           "\tie:%#x\n"
           "\tdata:%#x\n",
           opcode, phyaddr, regaddr, ie, data);

    /* We use default value --- PHY1
     * If driver operate on other PHYs, do nothing and
     * deceive it that the operation is finished
     */
    if ( phyaddr != 1 )
    {
        logout("Unsupport PHY address(phy = %#x)\n", phyaddr);
        goto done;
    }

    // 1: MDI write
    // 2: MDI read
    if ( opcode != MDI_WRITE && opcode != MDI_READ )
    {
        logout("Invalid Opcode(opcode = %#x)\n", opcode);
        return;
    }

    // Current only support MDI generic registers.
    if ( regaddr > 6 )
    {
        logout("Invalid phy register index( phy register addr = %#x)\n", regaddr);
    }

    if ( opcode == MDI_WRITE )
    {
        // MDI write
        switch ( regaddr )
        {
            case 0:    // Control Register
                if ( data & 0x8000 ) // Reset
                {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = e100_mdi_default[0];
                    s->mdimem[1] = e100_mdi_default[1];
                    data = s->mdimem[regaddr];
                }
                else
                {
                    /* Restart Auto Configuration = Normal Operation */
                    data &= ~0x0200;
                }
                break;
            case 1:    // Status Register
                logout("Invalid write on readonly register(opcode = %#x)\n", opcode);
                data = s->mdimem[regaddr];
                break;
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
                break;
        }
        s->mdimem[regaddr] = data;
        logout("MDI WRITE: reg = %#x, data = %#x\n", regaddr, data);
    }
    else if ( opcode == MDI_READ )
    {
        // MDI read
        switch ( regaddr )
        {
            case 0: // Control Register
                if ( data & 0x8000 ) // Reset
                {
                    /* Reset status and control registers to default. */
                    s->mdimem[0] = e100_mdi_default[0];
                    s->mdimem[1] = e100_mdi_default[1];
                }
                break;
            case 1: // Status Register
                // Auto Negotiation complete, set sticky bit to 1
                s->mdimem[regaddr] |= 0x0026;
                break;
            case 2: // PHY Identification Register (Word 1)
            case 3: // PHY Identification Register (Word 2)
                break;
            case 5: // Auto-Negotiation Link Partner Ability Register
                s->mdimem[regaddr] = 0x41fe;
                break;
            case 6: // Auto-Negotiation Expansion Register
                s->mdimem[regaddr] = 0x0001;
                break;
        }
        data = s->mdimem[regaddr];
        logout("MDI READ: reg = %#x, data = %#x\n", regaddr, data);
    }

    /* Emulation takes no time to finish MDI transaction.
     * Set MDI bit in SCB status register. */
done:
    val |= BIT(28);
    val = (val & 0xffff0000) + data;
    CSR_WRITE(SCB_MDI, val, uint32_t);

    if ( ie )
        e100_interrupt(s, (uint16_t)INT_MDI);
}

static void scb_mdi_func(E100State *s, uint32_t val, int dir)
{
    if ( dir == OP_READ )
        // Do nothing, just tell driver we are ready
        CSR_VAL(CSR_MDI) |= BIT(28);
    else if ( dir == OP_WRITE )
        e100_write_mdi(s, val);
    else
        logout("Invalid operation direction(dir=%x)\n", dir);

}

static void eeprom_reset(E100State *s, int type)
{
    eeprom_t *e = &s->eeprom;

    if ( type == EEPROM_RESET_ALL )
    {
        memset(e, 0x0, sizeof(eeprom_t));
        e->val_type = NOP;
        logout("EEPROM reset all\n");
        return;
    }

    CSR(CSR_EEPROM, eedo) = 1;
    e->start_bit = 0;
    e->opcode = 0;
    e->address = 0;
    e->data = 0;

    e->val = 0;
    e->val_len = 0;
    e->val_type = NOP;

    e->cs = 0;
    e->sk = 0;
    logout("EEPROM select reset\n");
}

static void do_eeprom_op(E100State *s, eeprom_t *e, int cs, int sk, int di, int dir)
{
    int assert_cs = (cs == 1 && e->cs == 0);
    int de_assert_cs = (cs == 0 && e->cs == 1);
    int de_assert_sk = (sk == 0 && e->sk == 1);

    // Chip select is not be enabled
    if ( cs == 0 && e->cs == 0 )
    {
        logout("Invalid EECS signal\n");
        return;
    }

    // update state
    e->cs = cs;
    e->sk = sk;

    // Do nothing
    if ( assert_cs )
    {
        logout("EECS assert\n");
        return;
    }

    // Complete one command
    if ( de_assert_cs )
    {
        if ( e->val_type == DATA && e->opcode == EEPROM_WRITE )
        {
            e->data = e->val;
            memcpy((void *)((unsigned long)e->contents + e->address),
                    &e->data, sizeof(e->data));
            logout("EEPROM write complete(data=%#x)\n", e->data);
        }
        eeprom_trace(0,0,0,NOP,1);
        eeprom_reset(s, EEPROM_SELECT_RESET);
        logout("EECS de-asserted\n");
        return;
    }

    // Chip is selected and serial clock is change, so the operation is vaild
    if ( cs == 1 && de_assert_sk == 1)
    {
        // Set start bit
        if ( e->start_bit == 0 && di == 1 )
        {
             e->start_bit = di;
             e->val_len = 0;
             e->val = 0;
             e->val_type = OPCODE;

             eeprom_trace(0,0,0,OPCODE,1);
             logout("EEPROM start bit set\n");
             return;
        }
        // Data in DI is vaild
        else if ( e->start_bit == 1 )
        {
            // If current operation is eeprom read, ignore DI
            if ( !(e->val_type == DATA && e->opcode == EEPROM_READ) )
            {
                e->val = (e->val << 1) | di;
                e->val_len ++;
            }

            switch ( e->val_type )
            {
                // Get the opcode.
                case OPCODE:
                    eeprom_trace(CSR(CSR_EEPROM, eedo), di, e->opcode, 0, 0);
                    if ( e->val_len  == 2 )
                    {
                        e->opcode = e->val;
                        e->val = 0;
                        e->val_len = 0;
                        e->val_type = ADDR;

                        eeprom_trace(0,0,0,ADDR,1);
                        logout("EEPROM get opcode(opcode name=%s,opcode=%#x )\n",
                                EEPROM_OPCODE_NAME(e->opcode), e->opcode);
                    }
                    break;
                // Get address
                case ADDR:
                    eeprom_trace(CSR(CSR_EEPROM, eedo), di, e->opcode, 0, 0);
                    if ( e->val_len == e->addr_len )
                    {
                        e->address = e->val;
                        e->val = 0;
                        e->val_len = 0;
                        e->val_type = DATA;

                        // We prepare data eary for later read operation
                        if ( e->opcode == EEPROM_READ )
                        {
                            memcpy(&e->data, (void *)(e->contents + e->address),
                                    sizeof(e->data));
                            logout("EEPROM prepare data to read(addr=%#x,data=%#x)\n", 
                                    e->address, e->data);
                        }

                        // Write dummy 0 to response to driver the address is written complete
                        CSR(CSR_EEPROM, eedo) = 0;
                        eeprom_trace(0,0,0,DATA,1);
                        logout("EEPROM get address(addr=%#x)\n", e->address);
                    }
                    break;
                // Only do data out operation
                case DATA:
                    if ( e->opcode == EEPROM_READ )
                    {
                        // Start from the most significant bit
                        //uint16_t t = ((e->data & (1<<(sizeof(e->data)*8 - e->val_len - 1))) != 0);
                        uint16_t t = !!(e->data & (0x8000U >> e->val_len));

                        CSR(CSR_EEPROM, eedo) = t;

                        logout("EEPROM read(reg address=%#x, reg val=%#x, do=%#x, len=%#x)\n", 
                                e->address, e->data, t, e->val_len);

                        if ( e->val_len > sizeof(e->data)*8 )
                        {
                            /* Driver may do more write op to de-assert EESK,
                             * So we let EEPROM go to idle after a register be
                             * read complete
                             */
                            e->val_type = NOP;
                            logout("Read complete\n");

                            break;
                        }

                        e->val_len ++;
                    }
                    eeprom_trace(CSR(CSR_EEPROM, eedo), di, e->opcode, 0, 0);
                    // Do eerpom write when CS de-assert
                    break;
                default:
                    break;
            }
        }
    }

    return;
}


static void scb_eeprom_func(E100State *s, uint32_t val, int dir)
{
    int eecs = ((val & EEPROM_CS) != 0);
    int eesk = ((val & EEPROM_SK) != 0);
    int eedi = ((val & EEPROM_DI) != 0);

    logout("EEPROM: Old(cs=%#x, sk=%#x), New(cs=%#x, sk=%#x, di=%#x)\n", 
            s->eeprom.cs, s->eeprom.sk, eecs, eesk, eedi);

    do_eeprom_op(s, &s->eeprom, eecs, eesk, eedi, dir);

    return;
}

static void e100_ru_command(E100State *s, uint8_t val)
{
    switch ( val )
    {
        case RU_NOP:
            /* Will not be here */
            break;
        case RU_START:
            /* RU start */

            SET_RU_STATE(RU_READY);
            logout("RU is set to ready\n");
            s->ru_offset = CSR_VAL(CSR_POINTER);
            logout("RFD offset is at %#x\n", s->ru_offset);
            break;
        case RU_RESUME:
            /* RU Resume */
            if ( GET_RU_STATE == RU_SUSPENDED )
                SET_RU_STATE(RU_READY);
            logout("RU resume to ready\n");
            break;
        case RU_ADDR_LOAD:
            /* Load RU base */
            s->ru_base = CSR_VAL(CSR_POINTER);
            logout("Load RU base address at %#x\n", s->ru_base);
            break;
        case RU_DMA_REDIRECT:
            logout("RU DMA redirect not implemented\n");
            break;
        case RU_ABORT:
            e100_interrupt(s, INT_RNR);
            SET_RU_STATE(RU_IDLE);
            logout("RU abort, go to idle\n");
            break;
        case RU_LOAD_HDS:
            logout("RU load header data size(HDS) not implemented\n");
        default:
            break;
    }
}

// This function will change CU's state, so CU start and
// CU resume must set CU's state before it
static void e100_execute_cb_list(E100State *s, int is_resume)
{

    struct control_block cb = {0};
    uint32_t cb_addr;

    if ( !is_resume )
        s->cu_offset = CSR_VAL(CSR_POINTER);

    /* If call from CU resume, cu_offset has been set */

    while (1)
    {
        cb_addr = s->cu_base + s->cu_offset;
        cpu_physical_memory_read(cb_addr, (uint8_t *)&cb, sizeof(cb));


        switch ( cb.cmd )
        {
            case CBL_NOP:
                /* Do nothing */
                break;
            case CBL_IASETUP:
                cpu_physical_memory_read(cb_addr + 8, &s->macaddr[0], sizeof(s->macaddr));
                e100_dump("Setup Individual Address:", &s->macaddr[0], 6);
                break;
            case CBL_CONFIGURE:
                {
                    i82557_cfg_t *cfg = &s->config;

                    assert(sizeof(s->config) == 22);
                    cpu_physical_memory_read(cb_addr + 8, (uint8_t *)cfg, sizeof(s->config));
                    logout("Setup card configuration:"
                            "\tbyte count:%d\n"
                            "\tRx FIFO limit:%d\n"
                            "\tTx FIFO limit:%d\n"
                            "\tAdaptive interframe spacing:%d\n"
                            "\tRx DMA max:%d\n"
                            "\tTX DMA max:%d\n"
                            "\tDMBC enable:%d\n"
                            "\tLate SCB:%d\n"
                            "\tTNO:%d\n"
                            "\tCI:%d\n"
                            "\tDiscard overrun RX:%d\n"
                            "\tSave bad frame:%d\n"
                            "\tDiscard short RX:%d\n"
                            "\tunderrun retry:%d\n"
                            "\tMII:%d\n"
                            "\tNSAI:%d\n"
                            "\tPreamble len:%d\n"
                            "\tloopback:%d\n"
                            "\tliner pro:%d\n"
                            "\tPRI mode:%d\n"
                            "\tinterframe spacing:%d\n"
                            "\tpromiscuous:%d\n"
                            "\tbroadcast dis:%d\n"
                            "\tCRS CDT:%d\n"
                            "\tstripping:%d\n"
                            "\tpadding:%d\n"
                            "\tRX crc:%d\n"
                            "\tforce fdx:%d\n"
                            "\tfdx enable:%d\n"
                            "\tmultiple IA:%d\n"
                            "\tmulticast all:%d\n",
                        cfg->byte_count, cfg->rx_fifo_limit, cfg->tx_fifo_limit,
                        cfg->adpt_inf_spacing, cfg->rx_dma_max_bytes, cfg->tx_dma_max_bytes,
                        cfg->dmbc_en, cfg->late_scb, cfg->tno_intr, cfg->ci_intr,
                        cfg->dis_overrun_rx, cfg->save_bad_frame, cfg->dis_short_rx,
                        cfg->underrun_retry, cfg->mii, cfg->nsai, cfg->preamble_len,
                        cfg->loopback, cfg->linear_prio, cfg->pri_mode, cfg->interframe_spacing,
                        cfg->promiscuous, cfg->broadcast_dis, cfg->crs_cdt, cfg->strip,
                        cfg->padding, cfg->rx_crc, cfg->force_fdx, cfg->fdx_en,
                        cfg->mul_ia, cfg->mul_all);
                }
                break;
            case CBL_MULTCAST_ADDR_SETUP:
                {
                    uint16_t mult_list_count = 0;
                    uint16_t size = 0;

                    cpu_physical_memory_read(cb_addr + 8, (uint8_t *)&mult_list_count, 2);
                    mult_list_count = (mult_list_count << 2) >> 2;

                    if ( !mult_list_count )
                    {
                        logout("Multcast disabled(multicast count=0)\n");
                        s->is_multcast_enable = 0;
                        memset(s->mult_list, 0x0, sizeof(s->mult_list));
                        break;
                    }
                    size = mult_list_count > sizeof(s->mult_list) ?
                        sizeof(s->mult_list) : mult_list_count;
                    cpu_physical_memory_read(cb_addr + 12, &s->mult_list[0], size);

                    e100_dump("Setup Multicast list: ", &s->mult_list[0], size);
                    break;
                }
            case CBL_TRANSMIT:
                {
                    struct
                    {
                        struct control_block cb;
                        tbd_t tbd;
                    } __attribute__ ((packed)) tx;

                    struct
                    {
                        uint32_t addr;
                        uint16_t size;
                        uint16_t is_el_set;
                    } tx_buf = {0};

                    uint32_t tbd_array;
                    uint16_t tcb_bytes;
                    uint8_t sf;
                    int len = s->pkt_buf_len;

                    assert( len < sizeof(s->pkt_buf));

                    cpu_physical_memory_read(cb_addr, (uint8_t *)&tx, sizeof(tx));
                    tbd_array = le32_to_cpu(tx.tbd.tx_desc_addr);
                    tcb_bytes = le16_to_cpu(tx.tbd.tcb_bytes);
                    // Indicate use what mode to transmit(simple or flexible)
                    sf = tx.cb.rs3 & 0x1;

                    logout("Get a TBD:\n"
                            "\tTBD array address:%#x\n"
                            "\tTCB byte count:%#x\n"
                            "\tEOF:%#x\n"
                            "\tTransmit Threshold:%#x\n"
                            "\tTBD number:%#x\n"
                            "\tUse %s mode to send frame\n",
                            tbd_array, tcb_bytes, tx.tbd.eof,
                            tx.tbd.tx_threshold, tx.tbd.tbd_num,
                            sf ? "Flexible" : "Simple");

                    if ( !sf || tbd_array == (uint32_t)-1 )
                    {
                        /* Simple mode */

                        /* For simple mode, TCB bytes should not be zero.
                         * But we still check here for safety
                         */
                        if ( !tcb_bytes || tcb_bytes > sizeof(s->pkt_buf) )
                            break;

                        cpu_physical_memory_read(cb_addr+16, &s->pkt_buf[0], tcb_bytes);
                        len = tcb_bytes;
                        logout("simple mode(size=%d)\n", len);

                    }
                    else
                    {
                        /* Flexible mode */

                        /* For flexible mode, TBD num should not be zero.
                         * But we still check here for safety
                         */
                        if ( !tx.tbd.tbd_num )
                            break;

                        // I82557 don't support extend TCB
                        if ( s->device == i82557C || s->device == i82557B )
                        {
                            /* Standard TCB mode */

                            int i;

                            for ( i=0; i<tx.tbd.tbd_num; i++ )
                            {

                                cpu_physical_memory_read(tbd_array, (uint8_t *)&tx_buf,
                                        sizeof(tx_buf));
                                tx_buf.is_el_set &= 0x1;
                                tx_buf.size &= 0x7fff;
                                tbd_array += 8;

                                if ( tx_buf.size > sizeof(s->pkt_buf) - len )
                                {
                                    logout("Warning: Get a too big TBD, ignore it"
                                            "(buf addr %#x, size %d, el:%#x)\n",
                                            tx_buf.addr, tx_buf.size, tx_buf.is_el_set);
                                    continue;
                                }

                                cpu_physical_memory_read(tx_buf.addr, &s->pkt_buf[len],
                                        tx_buf.size);

                                logout("TBD (standard mode): buf addr %#x, size %d, el:%#x\n",
                                        tx_buf.addr, tx_buf.size, tx_buf.is_el_set);
                                len += tx_buf.size;

                                if ( tx_buf.is_el_set )
                                    break;
                            }

                        }
                        //FIXME: Extend mode is not be tested
                        else
                        {
                            /* Extend TCB mode */

                            /* A strandard TCB followed by two TBDs */
                            uint32_t tbd_addr = cb_addr+16;
                            int i = 0;


                            for ( ; i<2 && i<tx.tbd.tbd_num; i++ )
                            {

                                cpu_physical_memory_read(tbd_array, (uint8_t *)&tx_buf,
                                        sizeof(tx_buf));
                                tx_buf.is_el_set &= 0x1;
                                tbd_addr += 8;

                                /* From Intel's spec, size of TBD equal to zero
                                 * has same effect with EL bit set
                                 */
                                if ( tx_buf.size == 0 )
                                {
                                    tx_buf.is_el_set = 1;
                                    break;
                                }

                                if ( tx_buf.size + len > sizeof(s->pkt_buf) )
                                {
                                    logout("TX frame is too large, discarding it"
                                            "(buf addr=%#x, size=%#x)\n", tx_buf.addr,
                                            tx_buf.size);
                                    //continue;
                                    break;
                                }

                                logout("TBD (extended mode): buf addr %#08x, size %#04x, el:%#x\n",
                                        tx_buf.addr, tx_buf.size, tx_buf.is_el_set);
                                cpu_physical_memory_read(tx_buf.addr, &s->pkt_buf[len],
                                        tx_buf.size);

                                len += tx_buf.size;

                                if ( tx_buf.is_el_set )
                                    break;
                            }

                            /* In extend TCB mode, TDB array point to the thrid TBD
                             * if it is not NULL(0xffffffff) and EL bit of before
                             * two TBDs is not set
                             */
                            if ( tbd_array != (uint32_t)-1 && !tx_buf.is_el_set )
                            {
                                tbd_addr = tbd_array;

                                /* TBD number includes first two TBDs, so don't
                                 * initialize i here
                                 */
                                for ( ; i<tx.tbd.tbd_num; i++ )
                                {
                                    cpu_physical_memory_read(tbd_addr, (uint8_t *)&tx_buf,
                                            sizeof(tx_buf));
                                    tx_buf.is_el_set &= 0x1;
                                    tbd_addr += 8;

                                    cpu_physical_memory_read(tx_buf.addr, &s->pkt_buf[len],
                                            tx_buf.size);
                                    logout("TBD (extended mode): buf addr 0x%#08x, size 0x%#04x\n",
                                            tx_buf.addr, tx_buf.size);

                                    len += tx_buf.size;

                                    if ( tx_buf.is_el_set )
                                        break;
                                }
                            }
                        }
                    }


                    s->pkt_buf_len = len;

/* Below codes are used for Threshold. But with these logic, network of guest
 * getting bad performance. So I comment it and leave codes here to hope anyone
 * fix it
 */
#if 0
                    /* If threshold is set, only send packet when threshold
                     * bytes are read
                     */
                    if ( tx.tbd.tx_threshold && s->pkt_buf_len < tx.tbd.tx_threshold * 8 )
                    {
                        logout("Current data length in FIFO buffer:%d\n", s->pkt_buf_len);
                        break;
                    }
#endif

                    if ( s->pkt_buf_len )
                    {
                        qemu_send_packet(s->vc, s->pkt_buf, s->pkt_buf_len);
                        s->statistics.tx_good_frames ++;
                        logout("Send out frame successful(size=%d,"
                                "already sent %d frames)\n", s->pkt_buf_len,
                                s->statistics.tx_good_frames);
                        s->pkt_buf_len = 0;
                    }

                    e100_dump("Dest addr:", (uint8_t *)s->pkt_buf, 6);
                    e100_dump("Src addr:", (uint8_t *)(s->pkt_buf+6), 6);
                    e100_dump("type:", (uint8_t *)(s->pkt_buf+8), 2);

                    break;
                }
            case CBL_LOAD_MICROCODE:
#ifdef DEBUG_E100
                {
                    /* Don't support load marco code, just dump it */
                    #define MICRO_CODE_LEN 256
                    uint8_t micro_code[MICRO_CODE_LEN] = {0};
                    cpu_physical_memory_read(cb_addr+8, micro_code, MICRO_CODE_LEN);
                    e100_dump("Load micro code:", micro_code, MICRO_CODE_LEN);
                }
#endif
                break;
            case CBL_DUMP:
                logout("Control block dump\n");
                break;
            case CBL_DIAGNOSE:
                logout("Control block diagnose\n");
                break;
            default:
                logout("Unknown Control block command(val=%#x)\n", cb.cmd);
                break;
        }

        /* Now, we finished executing a command, update status of CB.
         * We always success
         */
        cb.c = 1;
        cb.ok = 1;
        // Only update C bit and OK bit field in TCB
        cpu_physical_memory_write(cb_addr, (uint8_t *)&cb, 2);

        logout("Finished a command from CB list:\n"
                "\tok:%d\n"
                "\tc:%d\n"
                "\tcommand name:%s(cmd=%#x)\n"
                "\ti:%d\n"
                "\ts:%d\n"
                "\tel:%d\n"
                "\tlink address:%#x\n",
                cb.ok, cb.c, CB_CMD_NAME(cb.cmd), cb.cmd,
                cb.i, cb.s, cb.el, cb.link_addr);

        if ( cb.i )
            e100_interrupt(s, (uint16_t)INT_CX_TNO);

        // Suspend CU
        if ( cb.s )
        {
            logout("CU go to suspend\n");
            SET_CU_STATE(CU_SUSPENDED);
            s->cu_next = cb.link_addr; // Save it for go on executing when resume

            // Trigger CNA interrupt only when CNA mode is configured
            if ( !(s->config.ci_intr) && cb.i )
                e100_interrupt(s, (uint16_t)INT_CNA);

            return;
        }

        // This is last command in CB list, CU go back to IDLE
        if ( cb.el )
        {
            logout("Command block list is empty, CU go to idle\n");
            SET_CU_STATE(CU_IDLE);
            /* Either in CNA mode or CI mode, interrupt need be triggered
             * when CU go to idle.
             */
            if ( cb.i )
                e100_interrupt(s, (uint16_t)INT_CNA);

            return;
        }

        s->cu_offset = le32_to_cpu(cb.link_addr); // get next CB offset
    }
}

static void dump_statistics(E100State * s, uint32_t complete_word)
{
    /* Dump statistical data. Most data is never changed by the emulation
     * and always 0.
     */
    s->statistics.complete_word = complete_word;
    cpu_physical_memory_write(s->statsaddr, (uint8_t *)&s->statistics, sizeof(s->statistics));

}

static void e100_cu_command(E100State *s, uint8_t val)
{

    switch ( val )
    {
        case CU_NOP:
            /* Will not be here */
            break;
        case CU_START:
            /* This strictly follow Intel's spec */
            if ( GET_CU_STATE != CU_IDLE && GET_CU_STATE != CU_SUSPENDED )
            {
                logout("Illegal CU start command. Device is not idle or suspend\n");
                return;
            }

            SET_CU_STATE(CU_LPQ_ACTIVE);
            logout("CU start\n");

            e100_execute_cb_list(s, 0);
            break;
        case CU_RESUME:
            {
                uint32_t previous_cb = s->cu_base + s->cu_offset;
                struct control_block cb;

                /* Resume from suspend */

                /* FIXME:From Intel's spec, CU resume from idle is
                 * forbidden, but e100 drive in linux
                 * indeed do this.
                 */
                if ( GET_CU_STATE == CU_IDLE )
                {
                    logout("Illegal resume form IDLE\n");
                }

                cpu_physical_memory_read(previous_cb, (uint8_t *)&cb,
                                        sizeof(cb));

                //FIXME: Need any speical handle when CU is active ?

                /* Driver must clean S bit in previous CB when
                 * it issue CU resume command
                 */
                if ( cb.s )
                {
                    logout("CU still in suspend\n");
                    break;
                }

                SET_CU_STATE(CU_LPQ_ACTIVE);
                if ( cb.el )
                {
                    logout("CB list is empty, CU just go to active\n");
                    break;
                }

                // Continue next command
                s->cu_offset = s->cu_next;

                e100_execute_cb_list(s, 1);

                logout("CU resume\n");
            }
            break;
        case CU_STATSADDR:
            /* Load dump counters address */
            s->statsaddr = CSR_VAL(CSR_POINTER);
            logout("Load Stats address at %#x\n", s->statsaddr);
            break;
        case CU_SHOWSTATS:
            /* Dump statistical counters */
            dump_statistics(s, 0xa005);
            logout("Execute dump statistics\n");
            break;
        case CU_CMD_BASE:
            /* Load CU base */
            s->cu_base = CSR_VAL(CSR_POINTER);
            logout("Load CU base at %x\n", s->cu_base);
            break;
        case CU_DUMPSTATS:
            /* Dump statistical counters and reset counters. */
            dump_statistics(s, 0xa007);
            memset(&s->statistics, 0x0, sizeof(s->statistics));
            logout("Execute dump and reset statistics\n");
            break;
        case CU_S_RESUME:
            /* CU static resume */
            logout("CU static resume is not implemented\n");
            break;
        default:
            logout("Unknown CU command(val=%#x)\n", val);
            break;
    }

}

static void scb_cmd_func(E100State *s, uint16_t val, int dir)
{
    /* ignore NOP operation */
    if ( val & 0x0f )
    {
        e100_ru_command(s, val & 0x0f);
        CSR(CSR_CMD, ru_cmd) = 0;
    }
    else if ( val & 0xf0 )
    {
        e100_cu_command(s, val & 0xf0);
        CSR(CSR_CMD, cu_cmd) = 0;
    }

}

enum
{
    WRITEB,
    WRITEW,
    WRITEL,
    OP_IS_READ,
} WRITE_BYTES;

/* Driver may issue a command by writting one 32bit-entry,
 * two 16bit-entries or four 8bit-entries. In late two case, we
 * must wait until driver finish writting to the highest byte. The parameter
 * 'bytes' means write action of driver(writeb, wirtew, wirtel)
 */
static void e100_execute(E100State *s, uint32_t addr_offset,
        uint32_t val, int dir, int bytes)
{

    switch ( addr_offset )
    {
        case SCB_STATUS:
            if ( bytes == WRITEB )
                break;
        case SCB_ACK:
            if ( dir == OP_WRITE )
            {
                uint8_t _val = 0;
                if ( bytes == WRITEB )
                    _val = (uint8_t)val;
                else if ( bytes == WRITEW )
                    _val = ((uint16_t)val) >> 8;
                else if ( bytes == WRITEL)
                {
                    // This should not be happen
                    _val = ((uint16_t)val) >> 8;
                    logout("WARNNING: Drvier write 4 bytes to CSR register at offset %d,"
                           "emulator may do things wrong!!!\n", addr_offset);
                }

                e100_interrupt_ack(s, _val);
            }
            break;
        case SCB_CMD:
            if ( dir == OP_WRITE )
                scb_cmd_func(s, val, dir);

/* I don't know whether there is any driver writes command words and
 * interrupt mask at same time by two bytes. This is not a regular operation.
 * but if we meet the case, below codes could copy with it. As far
 * as I know. windows's and linux's driver don't do this thing.
 */
#if 0
            if ( bytes == WRITEW && (val&0xff00) != 0 )
                ;
            else
                break;
#endif
            break;
        case SCB_INTERRUPT_MASK:
            if ( dir == OP_WRITE )
            {
                uint8_t _val = 0;
                if ( bytes == WRITEB )
                    _val = (uint8_t)val;
                else if ( bytes == WRITEW )
                    _val = (val & 0xff00) >> 8;
                else
                    logout("WARNNING: Drvier write 4 bytes to CSR register at offset %d,"
                           "emulator may do things wrong!!!\n", addr_offset);

                // Driver generates a software interrupt
                if ( _val & BIT(1) )
                    e100_interrupt(s, INT_SWI);
            }
            break;
        case SCB_PORT ... SCB_PORT + 3:
            if ( dir == OP_WRITE )
            {
                // Waitting for driver write to the highest byte
                if ( (bytes == WRITEB && addr_offset != SCB_PORT + 3) ||
                     (bytes == WRITEW && addr_offset != SCB_PORT + 2) )
                    break;

                scb_port_func(s, CSR_VAL(CSR_PORT), dir);
            }
            break;
        case SCB_MDI ... SCB_MDI + 3:
            if ( dir == OP_WRITE )
            {
                // Waitting for driver write to the highest byte
                if ( (bytes == WRITEB && addr_offset != SCB_MDI + 3) ||
                     (bytes == WRITEW && addr_offset != SCB_MDI + 2) )
                    break;
            }

            scb_mdi_func(s, CSR_VAL(CSR_MDI), dir);
            break;
        case SCB_EEPROM:
            if ( dir == OP_WRITE )
                scb_eeprom_func(s, val, dir);
            // Nothing need do when driver read EEPROM registers of CSR
            break;
        case SCB_POINTER:
            break;
        default:
            logout("Driver operate on CSR reg(offset=%#x,dir=%s,val=%#x)\n",
                    addr_offset, dir==OP_WRITE?"write":"read", val);
    }

}

/* MMIO access functions */
static uint8_t e100_read1(E100State * s, uint32_t addr_offset)
{
    uint8_t val = -1;

    if ( addr_offset + sizeof(val) >= sizeof(s->pci_mem.mem) )
    {
        logout("Invaild read, beyond memory boundary(addr:%#x)\n", addr_offset
                + s->region_base_addr[CSR_MEMORY_BASE]);
        return val;
    }


    e100_execute(s, addr_offset, val, OP_READ, OP_IS_READ);
    val = CSR_READ(addr_offset, uint8_t);
    logout("READ1: Register name = %s, addr_offset = %#x, val=%#x\n", SCBNAME(addr_offset), addr_offset, val);

    return val;
}

static uint16_t e100_read2(E100State * s, uint32_t addr_offset)
{
    uint16_t val = -1;

    if ( addr_offset + sizeof(val) >= sizeof(s->pci_mem.mem) )
    {
        logout("Invaild read, beyond memory boundary(addr:%#x)\n", addr_offset 
                + s->region_base_addr[CSR_MEMORY_BASE]);
        return val;
    }

    e100_execute(s, addr_offset, val, OP_READ, OP_IS_READ);
    val = CSR_READ(addr_offset, uint16_t);
    logout("READ2: Register name = %s, addr_offset = %#x, val=%#x\n", SCBNAME(addr_offset), addr_offset, val);

    return val;

}

static uint32_t e100_read4(E100State * s, uint32_t addr_offset)
{
    uint32_t val = -1;

    if ( addr_offset + sizeof(val) >= sizeof(s->pci_mem.mem) )
    {
        logout("Invaild read, beyond memory boundary(addr:%#x)\n", addr_offset 
                + s->region_base_addr[CSR_MEMORY_BASE]);
        return val;
    }

    e100_execute(s, addr_offset, val, OP_READ, OP_IS_READ);
    val = CSR_READ(addr_offset, uint32_t);
    logout("READ4: Register name = %s, addr_offset = %#x, val=%#x\n", SCBNAME(addr_offset), addr_offset, val);

    return val;

}

static uint32_t pci_mmio_readb(void *opaque, target_phys_addr_t addr)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_MEMORY_BASE];
    return e100_read1(s, addr);
}

static uint32_t pci_mmio_readw(void *opaque, target_phys_addr_t addr)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_MEMORY_BASE];
    return e100_read2(s, addr);
}

static uint32_t pci_mmio_readl(void *opaque, target_phys_addr_t addr)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_MEMORY_BASE];
    return e100_read4(s, addr);
}

static CPUReadMemoryFunc *pci_mmio_read[] = {
    pci_mmio_readb,
    pci_mmio_readw,
    pci_mmio_readl
};

static void e100_write1(E100State * s, uint32_t addr_offset, uint8_t val)
{
    if ( addr_offset + sizeof(val) >= sizeof(s->pci_mem.mem) )
    {
        logout("Invaild write, beyond memory boundary(addr = %#x, val = %#x\n", addr_offset
                + s->region_base_addr[CSR_MEMORY_BASE], val);
        return;
    }

    // SCB stauts is read-only word, can not be directly write
    if ( addr_offset == SCB_STATUS )
    {
        return;
    }
    // EEDO bit of eeprom register is read-only, can not be written;
    else if ( addr_offset == SCB_EEPROM )
    {
        int eedo = BIT(3) & CSR_VAL(CSR_EEPROM);
        CSR_WRITE(addr_offset, val, uint8_t);
        CSR(CSR_EEPROM, eedo) = !!(eedo & EEPROM_DO);

        logout("WRITE1: Register name = %s, addr_offset = %#x, val = %#x\n", SCBNAME(addr_offset),addr_offset, (uint8_t)CSR_VAL(CSR_EEPROM));
        return;
    }
    else
    {
        CSR_WRITE(addr_offset, val, uint8_t);
    }

    logout("WRITE1: Register name = %s, addr_offset = %#x, val = %#x\n", SCBNAME(addr_offset),addr_offset, val);
    return;
}

static void e100_write2(E100State * s, uint32_t addr_offset, uint16_t val)
{
    if ( addr_offset + sizeof(val) >= sizeof(s->pci_mem.mem) )
    {
        logout("Invaild write, beyond memory boundary(addr = %#x, val = %#x\n", addr_offset
                + s->region_base_addr[CSR_MEMORY_BASE], val);
        return;
    }

    // SCB stauts is readonly word, can not be directly write
    if ( addr_offset == SCB_STATUS )
    {
        uint8_t __val = val >> 8;
        CSR_WRITE(addr_offset+1, __val, uint8_t);
    }
    // EEDO bit of eeprom register is read-only, can not be written;
    else if ( addr_offset == SCB_EEPROM )
    {
        int eedo = BIT(3) & CSR_VAL(CSR_EEPROM);
        CSR_WRITE(addr_offset, val, uint16_t);
        CSR(CSR_EEPROM, eedo) = !!(eedo & EEPROM_DO);

        logout("WRITE1: Register name = %s, addr_offset = %#x, val = %#x\n", SCBNAME(addr_offset),addr_offset, CSR_VAL(CSR_EEPROM));
        return;
    }
    else
    {
        CSR_WRITE(addr_offset, val, uint16_t);
    }

    logout("WRITE2: Register name = %s, addr_offset = %#x, val = %#x\n", SCBNAME(addr_offset),addr_offset, val);
    return;
}

static void e100_write4(E100State * s, uint32_t addr_offset, uint32_t val)
{
    if ( addr_offset + sizeof(val) >= sizeof(s->pci_mem.mem) )
    {
        logout("Invaild write, beyond memory boundary(addr = %#x, val = %#x\n", addr_offset 
                + s->region_base_addr[CSR_MEMORY_BASE], val);
        return;
    }

    // SCB stauts is readonly word, can not be directly write
    if ( addr_offset == SCB_STATUS )
    {
        uint8_t __val[4] = {0};

        //FIXME: any un-aligned reference ?
        *(uint32_t *)&__val = val;

        CSR_WRITE(addr_offset+1, __val[1], uint8_t);
        CSR_WRITE(addr_offset+2, __val[2], uint8_t);
        CSR_WRITE(addr_offset+3, __val[3], uint8_t);
    }
    /* No write4 opertaion on EEPROM register */
    else
    {
        CSR_WRITE(addr_offset, val, uint32_t);
    }

    logout("WRITE4: Register name = %s, addr_offset = %#x, val = %#x\n", SCBNAME(addr_offset),addr_offset, val);
    return;
}

static void pci_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_MEMORY_BASE];
    e100_write1(s, addr, val);
    e100_execute(s, addr, val, OP_WRITE, WRITEB);
}

static void pci_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_MEMORY_BASE];
    e100_write2(s, addr, val);
    e100_execute(s, addr, val, OP_WRITE, WRITEW);
}

static void pci_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_MEMORY_BASE];
    e100_write4(s, addr, val);
    (void)e100_execute(s, addr, val, OP_WRITE, WRITEL);
}

static CPUWriteMemoryFunc *pci_mmio_write[] = {
    pci_mmio_writeb,
    pci_mmio_writew,
    pci_mmio_writel
};

static void pci_mmio_map(PCIDevice * pci_dev, int region_num,
                         uint32_t addr, uint32_t size, int type)
{
    PCIE100State *d = (PCIE100State *) pci_dev;

    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);

    if ( region_num == CSR_MEMORY_BASE ) {
        /* Map control / status registers. */
        cpu_register_physical_memory(addr, size, d->e100.mmio_index);
        d->e100.region_base_addr[region_num] = addr;
    }
}

/* IO access functions */
static void ioport_write1(void *opaque, uint32_t addr, uint32_t val)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_IO_BASE];
    e100_write1(s, addr, val);
    (void)e100_execute(s, addr, (uint32_t)val, OP_WRITE, WRITEB);
}

static void ioport_write2(void *opaque, uint32_t addr, uint32_t val)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_IO_BASE];
    e100_write2(s, addr, val);
    (void)e100_execute(s, addr, (uint32_t)val, OP_WRITE, WRITEW);
}

static void ioport_write4(void *opaque, uint32_t addr, uint32_t val)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_IO_BASE];
    e100_write4(s, addr, val);
    (void)e100_execute(s, addr, (uint32_t)val, OP_WRITE, WRITEL);
}

static uint32_t ioport_read1(void *opaque, uint32_t addr)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_IO_BASE];
    return e100_read1(s, addr);
}

static uint32_t ioport_read2(void *opaque, uint32_t addr)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_IO_BASE];
    return e100_read2(s, addr);
}

static uint32_t ioport_read4(void *opaque, uint32_t addr)
{
    E100State *s = opaque;
    addr -= s->region_base_addr[CSR_IO_BASE];
    return e100_read4(s, addr);
}

static void pci_ioport_map(PCIDevice * pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIE100State *d = (PCIE100State *) pci_dev;
    E100State *s = &d->e100;

    logout("region %d, addr=0x%08x, size=0x%08x, type=%d\n",
           region_num, addr, size, type);

    if ( region_num != 1 )
    {
        logout("Invaid region number!\n");
        return;
    }

    register_ioport_write(addr, size, 1, ioport_write1, s);
    register_ioport_read(addr, size, 1, ioport_read1, s);
    register_ioport_write(addr, size, 2, ioport_write2, s);
    register_ioport_read(addr, size, 2, ioport_read2, s);
    register_ioport_write(addr, size, 4, ioport_write4, s);
    register_ioport_read(addr, size, 4, ioport_read4, s);

    s->region_base_addr[region_num] = addr;
}

/* From FreeBSD */
#define POLYNOMIAL 0x04c11db6
static int compute_mcast_idx(const uint8_t *ep)
{
    uint32_t crc;
    int carry, i, j;
    uint8_t b;

    crc = 0xffffffff;
    for (i = 0; i < 6; i++) {
        b = *ep++;
        for (j = 0; j < 8; j++) {
            carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
            crc <<= 1;
            b >>= 1;
            if (carry)
                crc = ((crc ^ POLYNOMIAL) | carry);
        }
    }
    return (crc >> 26);
}

#ifdef USE_BUFFER_TCP
#define BUFFER_TCP_ENABLE_VALVE 3
#define BUFFER_TCP_FLUSH_VALVE 5
static int buffer_tcp(E100State *s, const uint8_t *pkt)
{
    uint16_t eth_type = 0;
    uint8_t ip_type = 0;

    if ( !pkt )
        return 0;

    //FIXME: any un-aligned reference ?
    eth_type = ntohs(*(uint16_t *)(pkt+12));

    if ( eth_type == 0x0800 )
    {
        /* Get an IP frame */
        ip_type = *(uint8_t *)(pkt+23);
        if ( ip_type == 0x6 )
        {
            /* Get a TCP frame */
            if ( !s->buffer_tcp_enable &&
                    (++ s->continuous_tcp_frame > BUFFER_TCP_ENABLE_VALVE) )
                s->buffer_tcp_enable = 1;

            if ( !s->buffer_tcp_enable )
                goto do_flush_tcp_frame;

            if ( ++ s->unflush_tcp_num >= BUFFER_TCP_FLUSH_VALVE )
                goto do_flush_tcp_frame;
            else
                return 0;
        }
        else
        {
            s->continuous_tcp_frame = 0;
            s->buffer_tcp_enable = 0;
        }
    }
    else
    {
        s->continuous_tcp_frame = 0;
        s->buffer_tcp_enable = 0;
    }

do_flush_tcp_frame:
    s->unflush_tcp_num = 0;
    return 1;
}
#else
static inline int buffer_tcp(E100State *s, const uint8_t *pkt)
{
    return 1;
}
#endif

/* Eerpro100 receive functions */
static int e100_can_receive(void *opaque)
{
    E100State *s = opaque;

    int is_ready = (GET_RU_STATE == RU_READY);
    logout("%s\n", is_ready ? "EEPro100 receiver is ready"
            : "EEPro100 receiver is not ready");
    return is_ready;
}

static void e100_receive(void *opaque, const uint8_t * buf, int size)
{
    E100State *s = opaque;
    uint32_t rfd_addr = 0;
    rfd_t rfd = {0};


    if ( GET_RU_STATE != RU_READY )
    {
        //logout("RU is not ready. Begin discarding frame(state=%x)\n", GET_RU_STATE);
        return;
    }

    rfd_addr = s->ru_base + s->ru_offset;
    cpu_physical_memory_read(rfd_addr, (uint8_t *)&rfd, sizeof(rfd_t));

    if ( size > MAX_ETH_FRAME_SIZE+4 )
    {
        /* Long frame and configuration byte 18/3 (long receive ok) not set:
         * Long frames are discarded. */
        logout("Discard long frame(size=%d)\n", size);

        return;
    }
    else if ( !memcmp(buf, s->macaddr, sizeof(s->macaddr)) )
    {
        /* The frame is for me */
        logout("Receive a frame for me(size=%d)\n", size);
        e100_dump("FRAME:", (uint8_t *)buf, size);
    }
    else if ( !memcmp(buf, broadcast_macaddr, sizeof(broadcast_macaddr)) )
    {
        if ( s->config.broadcast_dis && !s->config.promiscuous )
        {
            logout("Discard a broadcast frame\n");
            return;
        }

        /* Broadcast frame */
        rfd.status |= RX_IA_MATCH;
        logout("Receive a broadcast frame(size=%d)\n", size);
    }
    else if ( s->is_multcast_enable && (buf[0] & 0x1) )
    {
        int mcast_idx = compute_mcast_idx(buf);
        if ( !(s->mult_list[mcast_idx >> 3] & (1 << (mcast_idx & 7))) )
        {
            logout("Multicast address mismatch, discard\n");
            return;
        }
        logout("Receive a multicast frame(size=%d)\n", size);
    }
    else if ( size < 64 && (s->config.dis_short_rx) )
    {
        /* From Intel's spec, short frame should be discarded
         * when configuration byte 7/0 (discard short receive) set.
         * But this will cause frame lossing such as ICMP frame, ARP frame.
         * So we check is the frame for me before discarding short frame
         */

        /* Save Bad Frame bit */
        if ( s->config.save_bad_frame )
        {
            rfd.status |= RX_SHORT;
            s->statistics.rx_short_frame_errors ++;
        }
        logout("Receive a short frame(size=%d), discard it\n", size);
        return;
    }
    else if ( s->config.promiscuous )
    {
        /* Promiscuous: receive all. No address match */
        logout("Received frame in promiscuous mode(size=%d)\n", size);
        rfd.status |= RX_NO_MATCH;
    }
    else
    {
        e100_dump("Unknown frame, MAC = ", (uint8_t *)buf, 6);
        return;
    }
    e100_dump("Get frame, MAC = ", (uint8_t *)buf, 6);

    rfd.c = 1;
    rfd.ok = 1;
    rfd.f = 1;
    rfd.eof = 1;
    rfd.status &= ~RX_COLLISION;
    rfd.count = size;

    logout("Get a RFD configure:\n"
            "\tstatus:%#x\n"
            "\tok:%#x\n" "\tc:%#x\n" "\tsf:%#x\n"
            "\th:%#x\n" "\ts:%#x\n" "\tel:%#x\n"
            "\tlink add:%#x\n" "\tactual count:%#x\n"
            "\tf:%#x\n" "\teof:%#x\n" "\tsize:%#x\n",
            rfd.status, rfd.ok, rfd.c, rfd.sf, rfd.h,
            rfd.s, rfd.el, rfd.link_addr, rfd.count,
            rfd.f, rfd.eof, rfd.size);

    cpu_physical_memory_write(rfd_addr, (uint8_t *)&rfd, sizeof(rfd));
    cpu_physical_memory_write(rfd_addr + sizeof(rfd_t), buf, size);
    s->statistics.rx_good_frames ++;
    s->ru_offset = le32_to_cpu(rfd.link_addr);

    if ( buffer_tcp(s, buf) )
        e100_interrupt(s, INT_FR);

    if ( rfd.el || rfd.s )
    {
        /* Go to suspend */
        SET_RU_STATE(RU_SUSPENDED);
        e100_interrupt(s, INT_RNR);
        logout("RFD met S or EL bit set, RU go to suspend\n");
        return;
    }

    logout("Complete a frame receive(size = %d)\n", size);
    return;
}

static void eeprom_init(E100State *s)
{
    int i;
    int chksum = 0;
    /* Add 64 * 2 EEPROM. i82557 and i82558 support a 64 word EEPROM,
     * i82559 and later support 64 or 256 word EEPROM. */
    eeprom_reset(s, EEPROM_RESET_ALL);
    s->eeprom.addr_len = EEPROM_I82557_ADDRBIT;
    memcpy(s->eeprom.contents, eeprom_i82557, sizeof(eeprom_i82557));
    /* Driver is going to get MAC from eeprom*/
    memcpy((uint8_t *)s->eeprom.contents, s->macaddr, sizeof(s->macaddr));

    /* The last word in eeprom saving checksum value.
     * After we update MAC in eeprom, the checksum need be re-calculate
     * and saved at the end of eeprom
     */
    for ( i=0; i<(1<<s->eeprom.addr_len)-1; i++ )
        chksum += s->eeprom.contents[i];
    s->eeprom.contents[i] = 0xBABA - chksum;

}

static PCIDevice *e100_init(PCIBus * bus, NICInfo * nd,
        const char *name, uint32_t device)
{
    PCIE100State *d;
    E100State *s;

    logout("\n");

    d = (PCIE100State *) pci_register_device(bus, name,
            sizeof(PCIE100State), -1,
            NULL, NULL);

    s = &d->e100;
    s->device = device;
    s->pci_dev = &d->dev;

    pci_reset(s);


    /* Handler for memory-mapped I/O */
    d->e100.mmio_index =
        cpu_register_io_memory(0, pci_mmio_read, pci_mmio_write, s);

    //CSR Memory mapped base
    pci_register_io_region(&d->dev, 0, PCI_MEM_SIZE,
            PCI_ADDRESS_SPACE_MEM | PCI_ADDRESS_SPACE_MEM_PREFETCH,
            pci_mmio_map);
    //CSR I/O mapped base
    pci_register_io_region(&d->dev, 1, PCI_IO_SIZE, PCI_ADDRESS_SPACE_IO,
            pci_ioport_map);
    //Flash memory mapped base
    pci_register_io_region(&d->dev, 2, PCI_FLASH_SIZE, PCI_ADDRESS_SPACE_MEM,
            pci_mmio_map);

    memcpy(s->macaddr, nd->macaddr, 6);
    e100_dump("MAC ADDR", (uint8_t *)&s->macaddr[0], 6);

    eeprom_init(s);

    e100_reset(s);

    s->vc = qemu_new_vlan_client(nd->vlan, nd->model, nd->name,
                                 e100_receive, e100_can_receive, s);

    qemu_format_nic_info_str(s->vc, s->macaddr);

    qemu_register_reset(e100_reset, s);

    register_savevm(name, 0, 3, e100_save, e100_load, s);

    return (PCIDevice *)d;
}

PCIDevice *pci_e100_init(PCIBus * bus, NICInfo * nd, int devfn)
{
    return e100_init(bus, nd, "e100", i82557C);
}

