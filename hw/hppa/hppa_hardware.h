/* HPPA cores and system support chips.  */

#ifndef HW_HPPA_HPPA_HARDWARE_H
#define HW_HPPA_HPPA_HARDWARE_H

#define FIRMWARE_START  0xf0000000
#define FIRMWARE_END    0xf0800000

#define DEVICE_HPA_LEN  0x00100000

#define GSC_HPA         0xffc00000
#define DINO_HPA        0xfff80000
#define DINO_UART_HPA   0xfff83000
#define  DINO_UART_BASE 0xfff83800
#define DINO_SCSI_HPA   0xfff8c000
#define LASI_HPA        0xffd00000
#define LASI_UART_HPA   0xffd05000
#define LASI_SCSI_HPA   0xffd06000
#define LASI_LAN_HPA    0xffd07000
#define LASI_LPT_HPA    0xffd02000
#define LASI_AUDIO_HPA  0xffd04000
#define LASI_PS2KBD_HPA 0xffd08000
#define LASI_PS2MOU_HPA 0xffd08100
#define LASI_GFX_HPA    0xf8000000
#define CPU_HPA         0xfffb0000
#define MEMORY_HPA      0xfffbf000

#define PCI_HPA         DINO_HPA        /* PCI bus */
#define IDE_HPA         0xf9000000      /* Boot disc controller */

/* offsets to DINO HPA: */
#define DINO_PCI_ADDR           0x064
#define DINO_CONFIG_DATA        0x068
#define DINO_IO_DATA            0x06c

#define PORT_PCI_CMD    (PCI_HPA + DINO_PCI_ADDR)
#define PORT_PCI_DATA   (PCI_HPA + DINO_CONFIG_DATA)

#define PORT_SERIAL1    (DINO_UART_HPA + 0x800)
#define PORT_SERIAL2    (LASI_UART_HPA + 0x800)

#define HPPA_MAX_CPUS   8       /* max. number of SMP CPUs */
#define CPU_CLOCK_MHZ   250     /* emulate a 250 MHz CPU */

#endif
