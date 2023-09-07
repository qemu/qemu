#ifndef HW_PPC_H
#define HW_PPC_H

#include "target/ppc/cpu-qom.h"

void ppc_set_irq(PowerPCCPU *cpu, int n_IRQ, int level);
PowerPCCPU *ppc_get_vcpu_by_pir(int pir);
int ppc_cpu_pir(PowerPCCPU *cpu);
int ppc_cpu_tir(PowerPCCPU *cpu);

/* PowerPC hardware exceptions management helpers */
typedef void (*clk_setup_cb)(void *opaque, uint32_t freq);
typedef struct clk_setup_t clk_setup_t;
struct clk_setup_t {
    clk_setup_cb cb;
    void *opaque;
};
static inline void clk_setup (clk_setup_t *clk, uint32_t freq)
{
    if (clk->cb != NULL)
        (*clk->cb)(clk->opaque, freq);
}

struct ppc_tb_t {
    /* Time base management */
    int64_t  tb_offset;    /* Compensation                    */
    int64_t  atb_offset;   /* Compensation                    */
    int64_t  vtb_offset;
    uint32_t tb_freq;      /* TB frequency                    */
    /* Decrementer management */
    uint64_t decr_next;    /* Tick for next decr interrupt    */
    uint32_t decr_freq;    /* decrementer frequency           */
    QEMUTimer *decr_timer;
    /* Hypervisor decrementer management */
    uint64_t hdecr_next;    /* Tick for next hdecr interrupt  */
    QEMUTimer *hdecr_timer;
    int64_t purr_offset;
    void *opaque;
    uint32_t flags;
};

/* PPC Timers flags */
#define PPC_TIMER_BOOKE              (1 << 0) /* Enable Booke support */
#define PPC_TIMER_E500               (1 << 1) /* Enable e500 support */
#define PPC_DECR_UNDERFLOW_TRIGGERED (1 << 2) /* Decr interrupt triggered when
                                               * the most significant bit
                                               * changes from 0 to 1.
                                               */
#define PPC_DECR_ZERO_TRIGGERED      (1 << 3) /* Decr interrupt triggered when
                                               * the decrementer reaches zero.
                                               */
#define PPC_DECR_UNDERFLOW_LEVEL     (1 << 4) /* Decr interrupt active when
                                               * the most significant bit is 1.
                                               */

uint64_t cpu_ppc_get_tb(ppc_tb_t *tb_env, uint64_t vmclk, int64_t tb_offset);
void cpu_ppc_tb_init(CPUPPCState *env, uint32_t freq);
void cpu_ppc_tb_reset(CPUPPCState *env);
void cpu_ppc_tb_free(CPUPPCState *env);
void cpu_ppc_hdecr_init(CPUPPCState *env);
void cpu_ppc_hdecr_exit(CPUPPCState *env);

/* Embedded PowerPC DCR management */
typedef uint32_t (*dcr_read_cb)(void *opaque, int dcrn);
typedef void (*dcr_write_cb)(void *opaque, int dcrn, uint32_t val);
int ppc_dcr_init (CPUPPCState *env, int (*dcr_read_error)(int dcrn),
                  int (*dcr_write_error)(int dcrn));
int ppc_dcr_register (CPUPPCState *env, int dcrn, void *opaque,
                      dcr_read_cb drc_read, dcr_write_cb dcr_write);
clk_setup_cb ppc_40x_timers_init (CPUPPCState *env, uint32_t freq,
                                  unsigned int decr_excp);

/* Embedded PowerPC reset */
void ppc40x_core_reset(PowerPCCPU *cpu);
void ppc40x_chip_reset(PowerPCCPU *cpu);
void ppc40x_system_reset(PowerPCCPU *cpu);

#if defined(CONFIG_USER_ONLY)
static inline void ppc40x_irq_init(PowerPCCPU *cpu) {}
static inline void ppc6xx_irq_init(PowerPCCPU *cpu) {}
static inline void ppc970_irq_init(PowerPCCPU *cpu) {}
static inline void ppcPOWER7_irq_init(PowerPCCPU *cpu) {}
static inline void ppcPOWER9_irq_init(PowerPCCPU *cpu) {}
static inline void ppce500_irq_init(PowerPCCPU *cpu) {}
static inline void ppc_irq_reset(PowerPCCPU *cpu) {}
#else
void ppc40x_irq_init(PowerPCCPU *cpu);
void ppce500_irq_init(PowerPCCPU *cpu);
void ppc6xx_irq_init(PowerPCCPU *cpu);
void ppc970_irq_init(PowerPCCPU *cpu);
void ppcPOWER7_irq_init(PowerPCCPU *cpu);
void ppcPOWER9_irq_init(PowerPCCPU *cpu);
void ppc_irq_reset(PowerPCCPU *cpu);
#endif

/* PPC machines for OpenBIOS */
enum {
    ARCH_PREP = 0,
    ARCH_MAC99,
    ARCH_HEATHROW,
    ARCH_MAC99_U3,
};

#define FW_CFG_PPC_WIDTH        (FW_CFG_ARCH_LOCAL + 0x00)
#define FW_CFG_PPC_HEIGHT       (FW_CFG_ARCH_LOCAL + 0x01)
#define FW_CFG_PPC_DEPTH        (FW_CFG_ARCH_LOCAL + 0x02)
#define FW_CFG_PPC_TBFREQ       (FW_CFG_ARCH_LOCAL + 0x03)
#define FW_CFG_PPC_CLOCKFREQ    (FW_CFG_ARCH_LOCAL + 0x04)
#define FW_CFG_PPC_IS_KVM       (FW_CFG_ARCH_LOCAL + 0x05)
#define FW_CFG_PPC_KVM_HC       (FW_CFG_ARCH_LOCAL + 0x06)
#define FW_CFG_PPC_KVM_PID      (FW_CFG_ARCH_LOCAL + 0x07)
#define FW_CFG_PPC_NVRAM_ADDR   (FW_CFG_ARCH_LOCAL + 0x08)
#define FW_CFG_PPC_BUSFREQ      (FW_CFG_ARCH_LOCAL + 0x09)
#define FW_CFG_PPC_NVRAM_FLAT   (FW_CFG_ARCH_LOCAL + 0x0a)
#define FW_CFG_PPC_VIACONFIG    (FW_CFG_ARCH_LOCAL + 0x0b)

#define PPC_SERIAL_MM_BAUDBASE 399193

/* ppc_booke.c */
void ppc_booke_timers_init(PowerPCCPU *cpu, uint32_t freq, uint32_t flags);
#endif
