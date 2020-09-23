/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 * Copyright (C) 2016 Ben Herrenschmidt
 */

#ifndef PMU_H
#define PMU_H

#include "hw/misc/mos6522.h"
#include "hw/misc/macio/gpio.h"
#include "qom/object.h"

/*
 * PMU commands
 */

#define PMU_POWER_CTRL0            0x10  /* control power of some devices */
#define PMU_POWER_CTRL             0x11  /* control power of some devices */
#define PMU_ADB_CMD                0x20  /* send ADB packet */
#define PMU_ADB_POLL_OFF           0x21  /* disable ADB auto-poll */
#define PMU_WRITE_NVRAM            0x33  /* write non-volatile RAM */
#define PMU_READ_NVRAM             0x3b  /* read non-volatile RAM */
#define PMU_SET_RTC                0x30  /* set real-time clock */
#define PMU_READ_RTC               0x38  /* read real-time clock */
#define PMU_SET_VOLBUTTON          0x40  /* set volume up/down position */
#define PMU_BACKLIGHT_BRIGHT       0x41  /* set backlight brightness */
#define PMU_GET_VOLBUTTON          0x48  /* get volume up/down position */
#define PMU_PCEJECT                0x4c  /* eject PC-card from slot */
#define PMU_BATTERY_STATE          0x6b  /* report battery state etc. */
#define PMU_SMART_BATTERY_STATE    0x6f  /* report battery state (new way) */
#define PMU_SET_INTR_MASK          0x70  /* set PMU interrupt mask */
#define PMU_INT_ACK                0x78  /* read interrupt bits */
#define PMU_SHUTDOWN               0x7e  /* turn power off */
#define PMU_CPU_SPEED              0x7d  /* control CPU speed on some models */
#define PMU_SLEEP                  0x7f  /* put CPU to sleep */
#define PMU_POWER_EVENTS           0x8f  /* Send power-event commands to PMU */
#define PMU_I2C_CMD                0x9a  /* I2C operations */
#define PMU_RESET                  0xd0  /* reset CPU */
#define PMU_GET_BRIGHTBUTTON       0xd9  /* report brightness up/down pos */
#define PMU_GET_COVER              0xdc  /* report cover open/closed */
#define PMU_SYSTEM_READY           0xdf  /* tell PMU we are awake */
#define PMU_DOWNLOAD_STATUS        0xe2  /* Called by MacOS during boot... */
#define PMU_READ_PMU_RAM           0xe8  /* read the PMU RAM... ??? */
#define PMU_GET_VERSION            0xea  /* read the PMU version */

/* Bits to use with the PMU_POWER_CTRL0 command */
#define PMU_POW0_ON            0x80    /* OR this to power ON the device */
#define PMU_POW0_OFF           0x00    /* leave bit 7 to 0 to power it OFF */
#define PMU_POW0_HARD_DRIVE    0x04    /* Hard drive power
                                        * (on wallstreet/lombard ?) */

/* Bits to use with the PMU_POWER_CTRL command */
#define PMU_POW_ON             0x80    /* OR this to power ON the device */
#define PMU_POW_OFF            0x00    /* leave bit 7 to 0 to power it OFF */
#define PMU_POW_BACKLIGHT      0x01    /* backlight power */
#define PMU_POW_CHARGER        0x02    /* battery charger power */
#define PMU_POW_IRLED          0x04    /* IR led power (on wallstreet) */
#define PMU_POW_MEDIABAY       0x08    /* media bay power
                                        * (wallstreet/lombard ?) */

/* Bits in PMU interrupt and interrupt mask bytes */
#define PMU_INT_PCEJECT        0x04    /* PC-card eject buttons */
#define PMU_INT_SNDBRT         0x08    /* sound/brightness up/down buttons */
#define PMU_INT_ADB            0x10    /* ADB autopoll or reply data */
#define PMU_INT_BATTERY        0x20    /* Battery state change */
#define PMU_INT_ENVIRONMENT    0x40    /* Environment interrupts */
#define PMU_INT_TICK           0x80    /* 1-second tick interrupt */

/* Other bits in PMU interrupt valid when PMU_INT_ADB is set */
#define PMU_INT_ADB_AUTO           0x04    /* ADB autopoll, when PMU_INT_ADB */
#define PMU_INT_WAITING_CHARGER    0x01    /* ??? */
#define PMU_INT_AUTO_SRQ_POLL      0x02    /* ??? */

/* Bits in the environement message (either obtained via PMU_GET_COVER,
 * or via PMU_INT_ENVIRONMENT on core99 */
#define PMU_ENV_LID_CLOSED     0x01    /* The lid is closed */

/* I2C related definitions */
#define PMU_I2C_MODE_SIMPLE    0
#define PMU_I2C_MODE_STDSUB    1
#define PMU_I2C_MODE_COMBINED  2

#define PMU_I2C_BUS_STATUS     0
#define PMU_I2C_BUS_SYSCLK     1
#define PMU_I2C_BUS_POWER      2

#define PMU_I2C_STATUS_OK          0
#define PMU_I2C_STATUS_DATAREAD    1
#define PMU_I2C_STATUS_BUSY        0xfe

/* Kind of PMU (model) */
enum {
    PMU_UNKNOWN,
    PMU_OHARE_BASED,        /* 2400, 3400, 3500 (old G3 powerbook) */
    PMU_HEATHROW_BASED,     /* PowerBook G3 series */
    PMU_PADDINGTON_BASED,   /* 1999 PowerBook G3 */
    PMU_KEYLARGO_BASED,     /* Core99 motherboard (PMU99) */
    PMU_68K_V1,             /* 68K PMU, version 1 */
    PMU_68K_V2,             /* 68K PMU, version 2 */
};

/* PMU PMU_POWER_EVENTS commands */
enum {
    PMU_PWR_GET_POWERUP_EVENTS = 0x00,
    PMU_PWR_SET_POWERUP_EVENTS = 0x01,
    PMU_PWR_CLR_POWERUP_EVENTS = 0x02,
    PMU_PWR_GET_WAKEUP_EVENTS = 0x03,
    PMU_PWR_SET_WAKEUP_EVENTS = 0x04,
    PMU_PWR_CLR_WAKEUP_EVENTS = 0x05,
};

/* Power events wakeup bits */
enum {
    PMU_PWR_WAKEUP_KEY = 0x01,           /* Wake on key press */
    PMU_PWR_WAKEUP_AC_INSERT = 0x02,     /* Wake on AC adapter plug */
    PMU_PWR_WAKEUP_AC_CHANGE = 0x04,
    PMU_PWR_WAKEUP_LID_OPEN = 0x08,
    PMU_PWR_WAKEUP_RING = 0x10,
};

/*
 * This table indicates for each PMU opcode:
 * - the number of data bytes to be sent with the command, or -1
 *   if a length byte should be sent,
 * - the number of response bytes which the PMU will return, or
 *   -1 if it will send a length byte.
 */

static const int8_t pmu_data_len[256][2] = {
/*  0        1        2        3        4        5        6        7  */
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  1},{ 0,  1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{ 0,  0},
    {-1,  0},{ 0,  0},{ 2,  0},{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0, -1},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{ 0, -1},
    { 4,  0},{20,  0},{-1,  0},{ 3,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  4},{ 0, 20},{ 2, -1},{ 2,  1},{ 3, -1},{-1, -1},{-1, -1},{ 4,  0},
    { 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  1},{ 0,  1},{-1, -1},{ 1,  0},{ 1,  0},{-1, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 0,  0},{ 2,  0},{ 2,  0},{-1,  0},{ 1,  0},{ 3,  0},{ 1,  0},
    { 0,  1},{ 1,  0},{ 0,  2},{ 0,  2},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},
    { 2,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  3},{ 0,  3},{ 0,  2},{ 0,  8},{ 0, -1},{ 0, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 1,  0},{ 1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0, -1},{ 0, -1},{-1, -1},{-1, -1},{-1, -1},{ 5,  1},{ 4,  1},{ 4,  1},
    { 4,  0},{-1,  0},{ 0,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  5},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 1,  0},{ 2,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 0,  1},{ 0,  1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 2,  0},{ 2,  0},{ 2,  0},{ 4,  0},{-1,  0},{ 0,  0},{-1,  0},{-1,  0},
    { 1,  1},{ 1,  0},{ 3,  0},{ 2,  0},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
    { 0,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    { 1,  1},{ 1,  1},{-1, -1},{-1, -1},{ 0,  1},{ 0, -1},{-1, -1},{-1, -1},
    {-1,  0},{ 4,  0},{ 0,  1},{-1,  0},{-1,  0},{ 4,  0},{-1,  0},{-1,  0},
    { 3, -1},{-1, -1},{ 0,  1},{-1, -1},{ 0, -1},{-1, -1},{-1, -1},{ 0,  0},
    {-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},{-1,  0},
    {-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},{-1, -1},
};

/* Command protocol state machine */
typedef enum {
    pmu_state_idle, /* Waiting for command */
    pmu_state_cmd,  /* Receiving command */
    pmu_state_rsp,  /* Responding to command */
} PMUCmdState;

/* MOS6522 PMU */
struct MOS6522PMUState {
    /*< private >*/
    MOS6522State parent_obj;
};

#define TYPE_MOS6522_PMU "mos6522-pmu"
OBJECT_DECLARE_SIMPLE_TYPE(MOS6522PMUState, MOS6522_PMU)
/**
 * PMUState:
 * @last_b: last value of B register
 */

struct PMUState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;
    uint64_t frequency;
    qemu_irq via_irq;
    bool via_irq_state;

    /* PMU state */
    MOS6522PMUState mos6522_pmu;

    /* PMU low level protocol state */
    PMUCmdState cmd_state;
    uint8_t last_b;
    uint8_t cmd;
    uint32_t cmdlen;
    uint32_t rsplen;
    uint8_t cmd_buf_pos;
    uint8_t cmd_buf[128];
    uint8_t cmd_rsp_pos;
    uint8_t cmd_rsp_sz;
    uint8_t cmd_rsp[128];

    /* PMU events/interrupts */
    uint8_t intbits;
    uint8_t intmask;

    /* ADB */
    bool has_adb;
    ADBBusState adb_bus;
    uint8_t adb_reply_size;
    uint8_t adb_reply[ADB_MAX_OUT_LEN];

    /* RTC */
    uint32_t tick_offset;
    QEMUTimer *one_sec_timer;
    int64_t one_sec_target;

    /* GPIO */
    MacIOGPIOState *gpio;
};

#define TYPE_VIA_PMU "via-pmu"
OBJECT_DECLARE_SIMPLE_TYPE(PMUState, VIA_PMU)

#endif /* PMU_H */
