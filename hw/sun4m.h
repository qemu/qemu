#ifndef SUN4M_H
#define SUN4M_H

/* Devices used by sparc32 system.  */

/* iommu.c */
void *iommu_init(target_phys_addr_t addr, uint32_t version, qemu_irq irq);
void sparc_iommu_memory_rw(void *opaque, target_phys_addr_t addr,
                                 uint8_t *buf, int len, int is_write);
static inline void sparc_iommu_memory_read(void *opaque,
                                           target_phys_addr_t addr,
                                           uint8_t *buf, int len)
{
    sparc_iommu_memory_rw(opaque, addr, buf, len, 0);
}

static inline void sparc_iommu_memory_write(void *opaque,
                                            target_phys_addr_t addr,
                                            uint8_t *buf, int len)
{
    sparc_iommu_memory_rw(opaque, addr, buf, len, 1);
}

/* tcx.c */
void tcx_init(DisplayState *ds, target_phys_addr_t addr, uint8_t *vram_base,
              unsigned long vram_offset, int vram_size, int width, int height,
              int depth);

/* slavio_intctl.c */
void *slavio_intctl_init(target_phys_addr_t addr, target_phys_addr_t addrg,
                         const uint32_t *intbit_to_level,
                         qemu_irq **irq, qemu_irq **cpu_irq,
                         qemu_irq **parent_irq, unsigned int cputimer);
void slavio_pic_info(void *opaque);
void slavio_irq_info(void *opaque);

/* sbi.c */
void *sbi_init(target_phys_addr_t addr, qemu_irq **irq, qemu_irq **cpu_irq,
               qemu_irq **parent_irq);

/* sun4c_intctl.c */
void *sun4c_intctl_init(target_phys_addr_t addr, qemu_irq **irq,
                        qemu_irq *parent_irq);

/* slavio_timer.c */
void slavio_timer_init_all(target_phys_addr_t base, qemu_irq master_irq,
                           qemu_irq *cpu_irqs, unsigned int num_cpus);

/* slavio_serial.c */
SerialState *slavio_serial_init(target_phys_addr_t base, qemu_irq irq,
                                CharDriverState *chr1, CharDriverState *chr2);
void slavio_serial_ms_kbd_init(target_phys_addr_t base, qemu_irq irq,
                               int disabled);

/* slavio_misc.c */
void *slavio_misc_init(target_phys_addr_t base, target_phys_addr_t power_base,
                       qemu_irq irq);
void slavio_set_power_fail(void *opaque, int power_failing);

/* esp.c */
#define ESP_MAX_DEVS 7
void esp_scsi_attach(void *opaque, BlockDriverState *bd, int id);
void *esp_init(target_phys_addr_t espaddr,
               void *dma_opaque, qemu_irq irq, qemu_irq *reset);

/* cs4231.c */
void cs_init(target_phys_addr_t base, int irq, void *intctl);

/* sparc32_dma.c */
void *sparc32_dma_init(target_phys_addr_t daddr, qemu_irq parent_irq,
                       void *iommu, qemu_irq **dev_irq, qemu_irq **reset);
void ledma_memory_read(void *opaque, target_phys_addr_t addr,
                       uint8_t *buf, int len, int do_bswap);
void ledma_memory_write(void *opaque, target_phys_addr_t addr,
                        uint8_t *buf, int len, int do_bswap);
void espdma_memory_read(void *opaque, uint8_t *buf, int len);
void espdma_memory_write(void *opaque, uint8_t *buf, int len);

/* pcnet.c */
void lance_init(NICInfo *nd, target_phys_addr_t leaddr, void *dma_opaque,
                qemu_irq irq, qemu_irq *reset);

/* eccmemctl.c */
void *ecc_init(target_phys_addr_t base, uint32_t version);

#endif
