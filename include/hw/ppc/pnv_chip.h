#ifndef PPC_PNV_CHIP_H
#define PPC_PNV_CHIP_H

#include "hw/pci-host/pnv_phb4.h"
#include "hw/ppc/pnv_adu.h"
#include "hw/ppc/pnv_chiptod.h"
#include "hw/ppc/pnv_core.h"
#include "hw/ppc/pnv_homer.h"
#include "hw/ppc/pnv_n1_chiplet.h"
#include "hw/ssi/pnv_spi.h"
#include "hw/ppc/pnv_lpc.h"
#include "hw/ppc/pnv_occ.h"
#include "hw/ppc/pnv_psi.h"
#include "hw/ppc/pnv_sbe.h"
#include "hw/ppc/pnv_xive.h"
#include "hw/ppc/pnv_i2c.h"
#include "hw/sysbus.h"

OBJECT_DECLARE_TYPE(PnvChip, PnvChipClass,
                    PNV_CHIP)

struct PnvChip {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    uint32_t     chip_id;
    uint64_t     ram_start;
    uint64_t     ram_size;

    bool         big_core;
    bool         lpar_per_core;
    uint32_t     nr_cores;
    uint32_t     nr_threads;
    uint64_t     cores_mask;
    PnvCore      **cores;

    uint32_t     num_pecs;

    MemoryRegion xscom_mmio;
    MemoryRegion xscom;
    AddressSpace xscom_as;

    MemoryRegion *fw_mr;
    gchar        *dt_isa_nodename;
};

#define TYPE_PNV8_CHIP "pnv8-chip"
DECLARE_INSTANCE_CHECKER(Pnv8Chip, PNV8_CHIP,
                         TYPE_PNV8_CHIP)

struct Pnv8Chip {
    /*< private >*/
    PnvChip      parent_obj;

    /*< public >*/
    MemoryRegion icp_mmio;

    PnvLpcController lpc;
    Pnv8Psi      psi;
    PnvOCC       occ;
    PnvHomer     homer;

#define PNV8_CHIP_PHB3_MAX 4
    /*
     * The array is used to allow quick access to the phbs by
     * pnv_ics_get_child() and pnv_ics_resend_child().
     */
    PnvPHB       *phbs[PNV8_CHIP_PHB3_MAX];
    uint32_t     num_phbs;

    XICSFabric    *xics;
};

#define TYPE_PNV9_CHIP "pnv9-chip"
DECLARE_INSTANCE_CHECKER(Pnv9Chip, PNV9_CHIP,
                         TYPE_PNV9_CHIP)

struct Pnv9Chip {
    /*< private >*/
    PnvChip      parent_obj;

    /*< public >*/
    PnvADU       adu;
    PnvXive      xive;
    Pnv9Psi      psi;
    PnvLpcController lpc;
    PnvChipTOD   chiptod;
    PnvOCC       occ;
    PnvSBE       sbe;
    PnvHomer     homer;

    uint32_t     nr_quads;
    PnvQuad      *quads;

#define PNV9_CHIP_MAX_PEC 3
    PnvPhb4PecState pecs[PNV9_CHIP_MAX_PEC];

#define PNV9_CHIP_MAX_I2C 4
    PnvI2C      i2c[PNV9_CHIP_MAX_I2C];
};

/*
 * A SMT8 fused core is a pair of SMT4 cores.
 */
#define PNV9_PIR2FUSEDCORE(pir) (((pir) >> 3) & 0xf)
#define PNV9_PIR2CHIP(pir)      (((pir) >> 8) & 0x7f)

#define TYPE_PNV10_CHIP "pnv10-chip"
DECLARE_INSTANCE_CHECKER(Pnv10Chip, PNV10_CHIP,
                         TYPE_PNV10_CHIP)

struct Pnv10Chip {
    /*< private >*/
    PnvChip      parent_obj;

    /*< public >*/
    PnvADU       adu;
    PnvXive2     xive;
    Pnv9Psi      psi;
    PnvLpcController lpc;
    PnvChipTOD   chiptod;
    PnvOCC       occ;
    PnvSBE       sbe;
    PnvHomer     homer;
    PnvN1Chiplet     n1_chiplet;
#define PNV10_CHIP_MAX_PIB_SPIC 6
    PnvSpi pib_spic[PNV10_CHIP_MAX_PIB_SPIC];

    uint32_t     nr_quads;
    PnvQuad      *quads;

#define PNV10_CHIP_MAX_PEC 2
    PnvPhb4PecState pecs[PNV10_CHIP_MAX_PEC];

#define PNV10_CHIP_MAX_I2C 4
    PnvI2C       i2c[PNV10_CHIP_MAX_I2C];
};

#define PNV10_PIR2FUSEDCORE(pir) (((pir) >> 3) & 0xf)
#define PNV10_PIR2CHIP(pir)      (((pir) >> 8) & 0x7f)
#define PNV10_PIR2THREAD(pir)    (((pir) & 0x7f))

#define TYPE_PNV11_CHIP "pnv11-chip"
DECLARE_INSTANCE_CHECKER(Pnv11Chip, PNV11_CHIP,
                         TYPE_PNV11_CHIP)

/* Power11 core is same as Power10 */
typedef struct Pnv10Chip Pnv11Chip;

struct PnvChipClass {
    /*< private >*/
    SysBusDeviceClass parent_class;

    /*< public >*/
    uint64_t     chip_cfam_id;
    uint64_t     cores_mask;
    uint32_t     num_pecs;
    uint32_t     num_phbs;

    uint32_t     i2c_num_engines;
    const int    *i2c_ports_per_engine;

    DeviceRealize parent_realize;

    /* Get PIR and TIR values for a CPU thread identified by core/thread id */
    void (*get_pir_tir)(PnvChip *chip, uint32_t core_id, uint32_t thread_id,
                         uint32_t *pir, uint32_t *tir);
    void (*intc_create)(PnvChip *chip, PowerPCCPU *cpu, Error **errp);
    void (*intc_reset)(PnvChip *chip, PowerPCCPU *cpu);
    void (*intc_destroy)(PnvChip *chip, PowerPCCPU *cpu);
    void (*intc_print_info)(PnvChip *chip, PowerPCCPU *cpu, GString *buf);
    void* (*intc_get)(PnvChip *chip);
    ISABus *(*isa_create)(PnvChip *chip, Error **errp);
    void (*dt_populate)(PnvChip *chip, void *fdt);
    void (*pic_print_info)(PnvChip *chip, GString *buf);
    uint64_t (*xscom_core_base)(PnvChip *chip, uint32_t core_id);
    uint32_t (*xscom_pcba)(PnvChip *chip, uint64_t addr);
};

#endif
