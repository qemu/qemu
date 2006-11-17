/* temporary hack until proper Kconfig integration */
#define CONFIG_ACX_PCI 1
#define CONFIG_ACX_USB 1

#define ACX_RELEASE "v0.3.35"

/* set to 0 if you don't want any debugging code to be compiled in */
/* set to 1 if you want some debugging */
/* set to 2 if you want extensive debug log */
#define ACX_DEBUG 2
#define ACX_DEFAULT_MSG (L_ASSOC|L_INIT)

/* assume 32bit I/O width
 * (16bit is also compatible with Compact Flash) */
#define ACX_IO_WIDTH 32

/* Set this to 1 if you want monitor mode to use
 * phy header. Currently it is not useful anyway since we
 * don't know what useful info (if any) is in phy header.
 * If you want faster/smaller code, say 0 here */
#define WANT_PHY_HDR 0

/* whether to do Tx descriptor cleanup in softirq (i.e. not in IRQ
 * handler) or not. Note that doing it later does slightly increase
 * system load, so still do that stuff in the IRQ handler for now,
 * even if that probably means worse latency */
#define TX_CLEANUP_IN_SOFTIRQ 0

/* if you want very experimental 802.11 power save mode features */
#define POWER_SAVE_80211 0

/* if you want very early packet fragmentation bits and pieces */
#define ACX_FRAGMENTATION 0

/* Locking: */
/* very talkative */
/* #define PARANOID_LOCKING 1 */
/* normal (use when bug-free) */
#define DO_LOCKING 1
/* else locking is disabled! */

/* 0 - normal mode */
/* 1 - development/debug: probe for IEs on modprobe */
#define CMD_DISCOVERY 0
