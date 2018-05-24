#ifndef HW_DISPLAY_BOCHS_VBE_H
#define HW_DISPLAY_BOCHS_VBE_H

/*
 * bochs vesa bios extension interface
 */

#define VBE_DISPI_MAX_XRES              16000
#define VBE_DISPI_MAX_YRES              12000
#define VBE_DISPI_MAX_BPP               32

#define VBE_DISPI_INDEX_ID              0x0
#define VBE_DISPI_INDEX_XRES            0x1
#define VBE_DISPI_INDEX_YRES            0x2
#define VBE_DISPI_INDEX_BPP             0x3
#define VBE_DISPI_INDEX_ENABLE          0x4
#define VBE_DISPI_INDEX_BANK            0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH      0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT     0x7
#define VBE_DISPI_INDEX_X_OFFSET        0x8
#define VBE_DISPI_INDEX_Y_OFFSET        0x9
#define VBE_DISPI_INDEX_NB              0xa /* size of vbe_regs[] */
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa /* read-only, not in vbe_regs */

/* VBE_DISPI_INDEX_ID */
#define VBE_DISPI_ID0                   0xB0C0
#define VBE_DISPI_ID1                   0xB0C1
#define VBE_DISPI_ID2                   0xB0C2
#define VBE_DISPI_ID3                   0xB0C3
#define VBE_DISPI_ID4                   0xB0C4
#define VBE_DISPI_ID5                   0xB0C5

/* VBE_DISPI_INDEX_ENABLE */
#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_GETCAPS               0x02
#define VBE_DISPI_8BIT_DAC              0x20
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_NOCLEARMEM            0x80

/* only used by isa-vga, pci vga devices use a memory bar */
#define VBE_DISPI_LFB_PHYSICAL_ADDRESS  0xE0000000


/*
 * qemu extension: mmio bar (region 2)
 */

#define PCI_VGA_MMIO_SIZE     0x1000

/* vga register region */
#define PCI_VGA_IOPORT_OFFSET 0x400
#define PCI_VGA_IOPORT_SIZE   (0x3e0 - 0x3c0)

/* bochs vbe register region */
#define PCI_VGA_BOCHS_OFFSET  0x500
#define PCI_VGA_BOCHS_SIZE    (0x0b * 2)

/* qemu extension register region */
#define PCI_VGA_QEXT_OFFSET   0x600
#define PCI_VGA_QEXT_SIZE     (2 * 4)

/* qemu extension registers */
#define PCI_VGA_QEXT_REG_SIZE         (0 * 4)
#define PCI_VGA_QEXT_REG_BYTEORDER    (1 * 4)
#define  PCI_VGA_QEXT_LITTLE_ENDIAN   0x1e1e1e1e
#define  PCI_VGA_QEXT_BIG_ENDIAN      0xbebebebe

#endif /* HW_DISPLAY_BOCHS_VBE_H */
