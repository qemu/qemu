#ifndef HW_USB_EHCI_REGS_H
#define HW_USB_EHCI_REGS_H

/* Capability Registers Base Address - section 2.2 */
#define CAPLENGTH        0x0000  /* 1-byte, 0x0001 reserved */
#define HCIVERSION       0x0002  /* 2-bytes, i/f version # */
#define HCSPARAMS        0x0004  /* 4-bytes, structural params */
#define HCCPARAMS        0x0008  /* 4-bytes, capability params */
#define EECP             HCCPARAMS + 1
#define HCSPPORTROUTE1   0x000c
#define HCSPPORTROUTE2   0x0010

#define USBCMD           0x0000
#define USBCMD_RUNSTOP   (1 << 0)      // run / Stop
#define USBCMD_HCRESET   (1 << 1)      // HC Reset
#define USBCMD_FLS       (3 << 2)      // Frame List Size
#define USBCMD_FLS_SH    2             // Frame List Size Shift
#define USBCMD_PSE       (1 << 4)      // Periodic Schedule Enable
#define USBCMD_ASE       (1 << 5)      // Asynch Schedule Enable
#define USBCMD_IAAD      (1 << 6)      // Int Asynch Advance Doorbell
#define USBCMD_LHCR      (1 << 7)      // Light Host Controller Reset
#define USBCMD_ASPMC     (3 << 8)      // Async Sched Park Mode Count
#define USBCMD_ASPME     (1 << 11)     // Async Sched Park Mode Enable
#define USBCMD_ITC       (0x7f << 16)  // Int Threshold Control
#define USBCMD_ITC_SH    16            // Int Threshold Control Shift

#define USBSTS           0x0004
#define USBSTS_RO_MASK   0x0000003f
#define USBSTS_INT       (1 << 0)      // USB Interrupt
#define USBSTS_ERRINT    (1 << 1)      // Error Interrupt
#define USBSTS_PCD       (1 << 2)      // Port Change Detect
#define USBSTS_FLR       (1 << 3)      // Frame List Rollover
#define USBSTS_HSE       (1 << 4)      // Host System Error
#define USBSTS_IAA       (1 << 5)      // Interrupt on Async Advance
#define USBSTS_HALT      (1 << 12)     // HC Halted
#define USBSTS_REC       (1 << 13)     // Reclamation
#define USBSTS_PSS       (1 << 14)     // Periodic Schedule Status
#define USBSTS_ASS       (1 << 15)     // Asynchronous Schedule Status

/*
 *  Interrupt enable bits correspond to the interrupt active bits in USBSTS
 *  so no need to redefine here.
 */
#define USBINTR              0x0008
#define USBINTR_MASK         0x0000003f

#define FRINDEX              0x000c
#define CTRLDSSEGMENT        0x0010
#define PERIODICLISTBASE     0x0014
#define ASYNCLISTADDR        0x0018
#define ASYNCLISTADDR_MASK   0xffffffe0

#define CONFIGFLAG           0x0040

/*
 * Bits that are reserved or are read-only are masked out of values
 * written to us by software
 */
#define PORTSC_RO_MASK       0x007001c0
#define PORTSC_RWC_MASK      0x0000002a
#define PORTSC_WKOC_E        (1 << 22)    // Wake on Over Current Enable
#define PORTSC_WKDS_E        (1 << 21)    // Wake on Disconnect Enable
#define PORTSC_WKCN_E        (1 << 20)    // Wake on Connect Enable
#define PORTSC_PTC           (15 << 16)   // Port Test Control
#define PORTSC_PTC_SH        16           // Port Test Control shift
#define PORTSC_PIC           (3 << 14)    // Port Indicator Control
#define PORTSC_PIC_SH        14           // Port Indicator Control Shift
#define PORTSC_POWNER        (1 << 13)    // Port Owner
#define PORTSC_PPOWER        (1 << 12)    // Port Power
#define PORTSC_LINESTAT      (3 << 10)    // Port Line Status
#define PORTSC_LINESTAT_SH   10           // Port Line Status Shift
#define PORTSC_PRESET        (1 << 8)     // Port Reset
#define PORTSC_SUSPEND       (1 << 7)     // Port Suspend
#define PORTSC_FPRES         (1 << 6)     // Force Port Resume
#define PORTSC_OCC           (1 << 5)     // Over Current Change
#define PORTSC_OCA           (1 << 4)     // Over Current Active
#define PORTSC_PEDC          (1 << 3)     // Port Enable/Disable Change
#define PORTSC_PED           (1 << 2)     // Port Enable/Disable
#define PORTSC_CSC           (1 << 1)     // Connect Status Change
#define PORTSC_CONNECT       (1 << 0)     // Current Connect Status

#endif /* HW_USB_EHCI_REGS_H */
