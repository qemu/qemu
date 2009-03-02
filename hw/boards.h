/* Declarations for use by board files for creating devices.  */

#ifndef HW_BOARDS_H
#define HW_BOARDS_H

typedef void QEMUMachineInitFunc(ram_addr_t ram_size, int vga_ram_size,
                                 const char *boot_device,
                                 const char *kernel_filename,
                                 const char *kernel_cmdline,
                                 const char *initrd_filename,
                                 const char *cpu_model);

typedef struct QEMUMachine {
    const char *name;
    const char *desc;
    QEMUMachineInitFunc *init;
#define RAMSIZE_FIXED	(1 << 0)
    ram_addr_t ram_require;
    int nodisk_ok;
    int use_scsi;
    int max_cpus;
    struct QEMUMachine *next;
} QEMUMachine;

int qemu_register_machine(QEMUMachine *m);
void register_machines(void);

extern QEMUMachine *current_machine;

/* Axis ETRAX.  */
extern QEMUMachine bareetraxfs_machine;
extern QEMUMachine axisdev88_machine;

/* pc.c */
extern QEMUMachine pc_machine;
extern QEMUMachine isapc_machine;

/* ppc.c */
extern QEMUMachine prep_machine;
extern QEMUMachine core99_machine;
extern QEMUMachine heathrow_machine;
extern QEMUMachine ref405ep_machine;
extern QEMUMachine taihu_machine;
extern QEMUMachine bamboo_machine;
extern QEMUMachine mpc8544ds_machine;

/* mips_r4k.c */
extern QEMUMachine mips_machine;

/* mips_jazz.c */
extern QEMUMachine mips_magnum_machine;
extern QEMUMachine mips_pica61_machine;

/* mips_malta.c */
extern QEMUMachine mips_malta_machine;

/* mips_mipssim.c */
extern QEMUMachine mips_mipssim_machine;

/* shix.c */
extern QEMUMachine shix_machine;

/* r2d.c */
extern QEMUMachine r2d_machine;

/* sun4m.c */
extern QEMUMachine ss5_machine, ss10_machine, ss600mp_machine, ss20_machine;
extern QEMUMachine voyager_machine, ss_lx_machine, ss4_machine, scls_machine;
extern QEMUMachine sbook_machine;
extern QEMUMachine ss2_machine;
extern QEMUMachine ss1000_machine, ss2000_machine;

/* sun4u.c */
extern QEMUMachine sun4u_machine;
extern QEMUMachine sun4v_machine;
extern QEMUMachine niagara_machine;

/* integratorcp.c */
extern QEMUMachine integratorcp_machine;

/* versatilepb.c */
extern QEMUMachine versatilepb_machine;
extern QEMUMachine versatileab_machine;

/* realview.c */
extern QEMUMachine realview_machine;

/* spitz.c */
extern QEMUMachine akitapda_machine;
extern QEMUMachine spitzpda_machine;
extern QEMUMachine borzoipda_machine;
extern QEMUMachine terrierpda_machine;

/* omap_sx1.c */
extern QEMUMachine sx1_machine_v1;
extern QEMUMachine sx1_machine_v2;

/* palm.c */
extern QEMUMachine palmte_machine;

/* nseries.c */
extern QEMUMachine n800_machine;
extern QEMUMachine n810_machine;

/* gumstix.c */
extern QEMUMachine connex_machine;
extern QEMUMachine verdex_machine;

/* stellaris.c */
extern QEMUMachine lm3s811evb_machine;
extern QEMUMachine lm3s6965evb_machine;

/* an5206.c */
extern QEMUMachine an5206_machine;

/* mcf5208.c */
extern QEMUMachine mcf5208evb_machine;

/* dummy_m68k.c */
extern QEMUMachine dummy_m68k_machine;

/* mainstone.c */
extern QEMUMachine mainstone2_machine;

/* musicpal.c */
extern QEMUMachine musicpal_machine;

/* tosa.c */
extern QEMUMachine tosapda_machine;

#endif
