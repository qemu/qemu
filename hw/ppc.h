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

clk_setup_cb cpu_ppc_tb_init (CPUState *env, uint32_t freq);
/* Embedded PowerPC DCR management */
typedef uint32_t (*dcr_read_cb)(void *opaque, int dcrn);
typedef void (*dcr_write_cb)(void *opaque, int dcrn, uint32_t val);
int ppc_dcr_init (CPUState *env, int (*dcr_read_error)(int dcrn),
                  int (*dcr_write_error)(int dcrn));
int ppc_dcr_register (CPUState *env, int dcrn, void *opaque,
                      dcr_read_cb drc_read, dcr_write_cb dcr_write);
clk_setup_cb ppc_emb_timers_init (CPUState *env, uint32_t freq,
                                  unsigned int decr_excp);

/* Embedded PowerPC reset */
void ppc40x_core_reset (CPUState *env);
void ppc40x_chip_reset (CPUState *env);
void ppc40x_system_reset (CPUState *env);
void PREP_debug_write (void *opaque, uint32_t addr, uint32_t val);

extern CPUWriteMemoryFunc * const PPC_io_write[];
extern CPUReadMemoryFunc * const PPC_io_read[];
void PPC_debug_write (void *opaque, uint32_t addr, uint32_t val);

void ppc40x_irq_init (CPUState *env);
void ppce500_irq_init (CPUState *env);
void ppc6xx_irq_init (CPUState *env);
void ppc970_irq_init (CPUState *env);
void ppcPOWER7_irq_init (CPUState *env);

/* PPC machines for OpenBIOS */
enum {
    ARCH_PREP = 0,
    ARCH_MAC99,
    ARCH_HEATHROW,
    ARCH_MAC99_U3,
};

#define FW_CFG_PPC_WIDTH	(FW_CFG_ARCH_LOCAL + 0x00)
#define FW_CFG_PPC_HEIGHT	(FW_CFG_ARCH_LOCAL + 0x01)
#define FW_CFG_PPC_DEPTH	(FW_CFG_ARCH_LOCAL + 0x02)
#define FW_CFG_PPC_TBFREQ	(FW_CFG_ARCH_LOCAL + 0x03)
#define FW_CFG_PPC_IS_KVM       (FW_CFG_ARCH_LOCAL + 0x05)
#define FW_CFG_PPC_KVM_HC       (FW_CFG_ARCH_LOCAL + 0x06)
#define FW_CFG_PPC_KVM_PID      (FW_CFG_ARCH_LOCAL + 0x07)

#define PPC_SERIAL_MM_BAUDBASE 399193
