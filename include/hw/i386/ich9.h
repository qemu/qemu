#ifndef HW_ICH9_H
#define HW_ICH9_H

#include "hw/hw.h"
#include "qemu/range.h"
#include "hw/isa/isa.h"
#include "hw/sysbus.h"
#include "hw/i386/pc.h"
#include "hw/isa/apm.h"
#include "hw/i386/ioapic.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci_bridge.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ich9.h"
#include "hw/pci/pci_bus.h"

void ich9_lpc_set_irq(void *opaque, int irq_num, int level);
int ich9_lpc_map_irq(PCIDevice *pci_dev, int intx);
PCIINTxRoute ich9_route_intx_pin_to_irq(void *opaque, int pirq_pin);
void ich9_lpc_pm_init(PCIDevice *pci_lpc);
PCIBus *ich9_d2pbr_init(PCIBus *bus, int devfn, int sec_bus);
I2CBus *ich9_smb_init(PCIBus *bus, int devfn, uint32_t smb_io_base);

#define ICH9_CC_SIZE                            (16 * 1024)     /* 16KB */

#define TYPE_ICH9_LPC_DEVICE "ICH9-LPC"
#define ICH9_LPC_DEVICE(obj) \
     OBJECT_CHECK(ICH9LPCState, (obj), TYPE_ICH9_LPC_DEVICE)

typedef struct ICH9LPCState {
    /* ICH9 LPC PCI to ISA bridge */
    PCIDevice d;

    /* (pci device, intx) -> pirq
     * In real chipset case, the unused slots are never used
     * as ICH9 supports only D25-D32 irq routing.
     * On the other hand in qemu case, any slot/function can be populated
     * via command line option.
     * So fallback interrupt routing for any devices in any slots is necessary.
    */
    uint8_t irr[PCI_SLOT_MAX][PCI_NUM_PINS];

    APMState apm;
    ICH9LPCPMRegs pm;
    uint32_t sci_level; /* track sci level */

    /* 10.1 Chipset Configuration registers(Memory Space)
     which is pointed by RCBA */
    uint8_t chip_config[ICH9_CC_SIZE];

    /*
     * 13.7.5 RST_CNT---Reset Control Register (LPC I/F---D31:F0)
     *
     * register contents and IO memory region
     */
    uint8_t rst_cnt;
    MemoryRegion rst_cnt_mem;

    /* isa bus */
    ISABus *isa_bus;
    MemoryRegion rbca_mem;
    Notifier machine_ready;

    qemu_irq *pic;
    qemu_irq *ioapic;
} ICH9LPCState;

Object *ich9_lpc_find(void);

#define Q35_MASK(bit, ms_bit, ls_bit) \
((uint##bit##_t)(((1ULL << ((ms_bit) + 1)) - 1) & ~((1ULL << ls_bit) - 1)))

/* ICH9: Chipset Configuration Registers */
#define ICH9_CC_ADDR_MASK                       (ICH9_CC_SIZE - 1)

#define ICH9_CC
#define ICH9_CC_D28IP                           0x310C
#define ICH9_CC_D28IP_SHIFT                     4
#define ICH9_CC_D28IP_MASK                      0xf
#define ICH9_CC_D28IP_DEFAULT                   0x00214321
#define ICH9_CC_D31IR                           0x3140
#define ICH9_CC_D30IR                           0x3142
#define ICH9_CC_D29IR                           0x3144
#define ICH9_CC_D28IR                           0x3146
#define ICH9_CC_D27IR                           0x3148
#define ICH9_CC_D26IR                           0x314C
#define ICH9_CC_D25IR                           0x3150
#define ICH9_CC_DIR_DEFAULT                     0x3210
#define ICH9_CC_D30IR_DEFAULT                   0x0
#define ICH9_CC_DIR_SHIFT                       4
#define ICH9_CC_DIR_MASK                        0x7
#define ICH9_CC_OIC                             0x31FF
#define ICH9_CC_OIC_AEN                         0x1

/* D28:F[0-5] */
#define ICH9_PCIE_DEV                           28
#define ICH9_PCIE_FUNC_MAX                      6


/* D29:F0 USB UHCI Controller #1 */
#define ICH9_USB_UHCI1_DEV                      29
#define ICH9_USB_UHCI1_FUNC                     0

/* D30:F0 DMI-to-PCI bridge */
#define ICH9_D2P_BRIDGE                         "ICH9 D2P BRIDGE"
#define ICH9_D2P_BRIDGE_SAVEVM_VERSION          0

#define ICH9_D2P_BRIDGE_DEV                     30
#define ICH9_D2P_BRIDGE_FUNC                    0

#define ICH9_D2P_SECONDARY_DEFAULT              (256 - 8)

#define ICH9_D2P_A2_REVISION                    0x92

/* D31:F0 LPC Processor Interface */
#define ICH9_RST_CNT_IOPORT                     0xCF9

/* D31:F1 LPC controller */
#define ICH9_A2_LPC                             "ICH9 A2 LPC"
#define ICH9_A2_LPC_SAVEVM_VERSION              0

#define ICH9_LPC_DEV                            31
#define ICH9_LPC_FUNC                           0

#define ICH9_A2_LPC_REVISION                    0x2
#define ICH9_LPC_NB_PIRQS                       8       /* PCI A-H */

#define ICH9_LPC_PMBASE                         0x40
#define ICH9_LPC_PMBASE_BASE_ADDRESS_MASK       Q35_MASK(32, 15, 7)
#define ICH9_LPC_PMBASE_RTE                     0x1
#define ICH9_LPC_PMBASE_DEFAULT                 0x1
#define ICH9_LPC_ACPI_CTRL                      0x44
#define ICH9_LPC_ACPI_CTRL_ACPI_EN              0x80
#define ICH9_LPC_ACPI_CTRL_SCI_IRQ_SEL_MASK     Q35_MASK(8, 2, 0)
#define ICH9_LPC_ACPI_CTRL_9                    0x0
#define ICH9_LPC_ACPI_CTRL_10                   0x1
#define ICH9_LPC_ACPI_CTRL_11                   0x2
#define ICH9_LPC_ACPI_CTRL_20                   0x4
#define ICH9_LPC_ACPI_CTRL_21                   0x5
#define ICH9_LPC_ACPI_CTRL_DEFAULT              0x0

#define ICH9_LPC_PIRQA_ROUT                     0x60
#define ICH9_LPC_PIRQB_ROUT                     0x61
#define ICH9_LPC_PIRQC_ROUT                     0x62
#define ICH9_LPC_PIRQD_ROUT                     0x63

#define ICH9_LPC_PIRQE_ROUT                     0x68
#define ICH9_LPC_PIRQF_ROUT                     0x69
#define ICH9_LPC_PIRQG_ROUT                     0x6a
#define ICH9_LPC_PIRQH_ROUT                     0x6b

#define ICH9_LPC_PIRQ_ROUT_IRQEN                0x80
#define ICH9_LPC_PIRQ_ROUT_MASK                 Q35_MASK(8, 3, 0)
#define ICH9_LPC_PIRQ_ROUT_DEFAULT              0x80

#define ICH9_LPC_RCBA                           0xf0
#define ICH9_LPC_RCBA_BA_MASK                   Q35_MASK(32, 31, 14)
#define ICH9_LPC_RCBA_EN                        0x1
#define ICH9_LPC_RCBA_DEFAULT                   0x0

#define ICH9_LPC_PIC_NUM_PINS                   16
#define ICH9_LPC_IOAPIC_NUM_PINS                24

/* D31:F2 SATA Controller #1 */
#define ICH9_SATA1_DEV                          31
#define ICH9_SATA1_FUNC                         2

/* D30:F1 power management I/O registers
   offset from the address ICH9_LPC_PMBASE */

/* ICH9 LPC PM I/O registers are 128 ports and 128-aligned */
#define ICH9_PMIO_SIZE                          128
#define ICH9_PMIO_MASK                          (ICH9_PMIO_SIZE - 1)

#define ICH9_PMIO_PM1_STS                       0x00
#define ICH9_PMIO_PM1_EN                        0x02
#define ICH9_PMIO_PM1_CNT                       0x04
#define ICH9_PMIO_PM1_TMR                       0x08
#define ICH9_PMIO_GPE0_STS                      0x20
#define ICH9_PMIO_GPE0_EN                       0x28
#define ICH9_PMIO_GPE0_LEN                      16
#define ICH9_PMIO_SMI_EN                        0x30
#define ICH9_PMIO_SMI_EN_APMC_EN                (1 << 5)
#define ICH9_PMIO_SMI_STS                       0x34

/* FADT ACPI_ENABLE/ACPI_DISABLE */
#define ICH9_APM_ACPI_ENABLE                    0x2
#define ICH9_APM_ACPI_DISABLE                   0x3


/* D31:F3 SMBus controller */
#define ICH9_A2_SMB_REVISION                    0x02
#define ICH9_SMB_PI                             0x00

#define ICH9_SMB_SMBMBAR0                       0x10
#define ICH9_SMB_SMBMBAR1                       0x14
#define ICH9_SMB_SMBM_BAR                       0
#define ICH9_SMB_SMBM_SIZE                      (1 << 8)
#define ICH9_SMB_SMB_BASE                       0x20
#define ICH9_SMB_SMB_BASE_BAR                   4
#define ICH9_SMB_SMB_BASE_SIZE                  (1 << 5)
#define ICH9_SMB_HOSTC                          0x40
#define ICH9_SMB_HOSTC_SSRESET                  ((uint8_t)(1 << 3))
#define ICH9_SMB_HOSTC_I2C_EN                   ((uint8_t)(1 << 2))
#define ICH9_SMB_HOSTC_SMB_SMI_EN               ((uint8_t)(1 << 1))
#define ICH9_SMB_HOSTC_HST_EN                   ((uint8_t)(1 << 0))

/* D31:F3 SMBus I/O and memory mapped I/O registers */
#define ICH9_SMB_DEV                            31
#define ICH9_SMB_FUNC                           3

#define ICH9_SMB_HST_STS                        0x00
#define ICH9_SMB_HST_CNT                        0x02
#define ICH9_SMB_HST_CMD                        0x03
#define ICH9_SMB_XMIT_SLVA                      0x04
#define ICH9_SMB_HST_D0                         0x05
#define ICH9_SMB_HST_D1                         0x06
#define ICH9_SMB_HOST_BLOCK_DB                  0x07

#endif /* HW_ICH9_H */
