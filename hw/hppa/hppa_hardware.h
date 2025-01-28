/* HPPA cores and system support chips.  */
/* Be aware: QEMU and seabios-hppa repositories share this file as-is. */

#ifndef HW_HPPA_HPPA_HARDWARE_H
#define HW_HPPA_HPPA_HARDWARE_H

#define FIRMWARE_START  0xf0000000
#define FIRMWARE_END    0xf0800000
#define FIRMWARE_HIGH   0xfffffff0  /* upper 32-bits of 64-bit firmware address */

#define RAM_MAP_HIGH  0x0100000000  /* memory above 3.75 GB is mapped here */

#define MEM_PDC_ENTRY       0x4800  /* PDC entry address */

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
#define LASI_RTC_HPA    0xffd09000
#define LASI_LPT_HPA    0xffd02000
#define LASI_AUDIO_HPA  0xffd04000
#define LASI_PS2KBD_HPA 0xffd08000
#define LASI_PS2MOU_HPA 0xffd08100
#define LASI_GFX_HPA    0xf8000000
#define ARTIST_FB_ADDR  0xf9000000
#define CPU_HPA         0xfffb0000
#define MEMORY_HPA      0xfffff000

#define IDE_HPA         0xf9000000      /* Boot disc controller */
#define ASTRO_HPA       0xfed00000
#define ELROY0_HPA      0xfed30000
#define ELROY2_HPA      0xfed32000
#define ELROY8_HPA      0xfed38000
#define ELROYc_HPA      0xfed3c000
#define ASTRO_MEMORY_HPA 0xfed10200

#define SCSI_HPA        0xf1040000      /* emulated SCSI, needs to be in f region */

/* offsets to DINO HPA: */
#define DINO_PCI_ADDR           0x064
#define DINO_CONFIG_DATA        0x068
#define DINO_IO_DATA            0x06c

#define PORT_PCI_CMD    hppa_port_pci_cmd
#define PORT_PCI_DATA   hppa_port_pci_data

#define FW_CFG_IO_BASE  0xfffa0000

#define PORT_SERIAL1    (LASI_UART_HPA + 0x800)
#define PORT_SERIAL2    (DINO_UART_HPA + 0x800)

#define HPPA_MAX_CPUS   16      /* max. number of SMP CPUs */
#define CPU_CLOCK_MHZ   250     /* emulate a 250 MHz CPU */

#define CR_PSW_DEFAULT  6       /* used by SeaBIOS & QEMU for default PSW */
#define CPU_HPA_CR_REG  7       /* store CPU HPA in cr7 (SeaBIOS internal) */
#define PIM_STORAGE_SIZE 600	/* storage size of pdc_pim_toc_struct (64bit) */

#define ASTRO_BUS_MODULE        0x0a            /* C3700: 0x0a, others maybe 0 ? */

/* ASTRO Memory and I/O regions */
#define ASTRO_BASE_HPA            0xfffed00000
#define ELROY0_BASE_HPA           0xfffed30000  /* ELROY0_HPA */

#define ROPES_PER_IOC           8       /* per Ike half or Pluto/Astro */

#define LMMIO_DIRECT0_BASE  0x300
#define LMMIO_DIRECT0_MASK  0x308
#define LMMIO_DIRECT0_ROUTE 0x310

/* space register hashing */
#define HPPA64_DIAG_SPHASH_ENABLE       0x200   /* DIAG_SPHASH_ENAB (bit 54) */
#define HPPA64_PDC_CACHE_RET_SPID_VAL   0xfe0   /* PDC return value on 64-bit CPU */

#endif
