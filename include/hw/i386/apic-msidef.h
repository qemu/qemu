#ifndef HW_APIC_MSIDEF_H
#define HW_APIC_MSIDEF_H

/*
 * Intel APIC constants: from include/asm/msidef.h
 */

/*
 * Shifts for MSI data
 */

#define MSI_DATA_VECTOR_SHIFT           0
#define  MSI_DATA_VECTOR_MASK           0x000000ff

#define MSI_DATA_DELIVERY_MODE_SHIFT    8
#define MSI_DATA_LEVEL_SHIFT            14
#define MSI_DATA_TRIGGER_SHIFT          15

/*
 * Shift/mask fields for msi address
 */

#define MSI_ADDR_DEST_MODE_SHIFT        2

#define MSI_ADDR_REDIRECTION_SHIFT      3

#define MSI_ADDR_DEST_ID_SHIFT          12
#define MSI_ADDR_DEST_IDX_SHIFT         4
#define  MSI_ADDR_DEST_ID_MASK          0x00ffff0

#endif /* HW_APIC_MSIDEF_H */
