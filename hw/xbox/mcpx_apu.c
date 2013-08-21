/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"

#include "hw/xbox/mcpx_apu.h"



#define NV_PAPU_ISTS                                     0x00001000
#   define NV_PAPU_ISTS_GINTSTS                               (1 << 0)
#   define NV_PAPU_ISTS_FETINTSTS                             (1 << 4)
#define NV_PAPU_IEN                                      0x00001004
#define NV_PAPU_FECTL                                    0x00001100
#   define NV_PAPU_FECTL_FEMETHMODE                         0x000000E0
#       define NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING            0x00000000
#       define NV_PAPU_FECTL_FEMETHMODE_HALTED                  0x00000080
#       define NV_PAPU_FECTL_FEMETHMODE_TRAPPED                 0x000000E0
#   define NV_PAPU_FECTL_FETRAPREASON                       0x00000F00
#       define NV_PAPU_FECTL_FETRAPREASON_REQUESTED             0x00000F00
#define NV_PAPU_FECV                                     0x00001110
#define NV_PAPU_FEAV                                     0x00001118
#   define NV_PAPU_FEAV_VALUE                               0x0000FFFF
#   define NV_PAPU_FEAV_LST                                 0x00030000
#define NV_PAPU_FEDECMETH                                0x00001300
#define NV_PAPU_FEDECPARAM                               0x00001304
#define NV_PAPU_FEMEMADDR                                0x00001324
#define NV_PAPU_FEMEMDATA                                0x00001334
#define NV_PAPU_FETFORCE0                                0x00001500
#define NV_PAPU_FETFORCE1                                0x00001504
#   define NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE               (1 << 15)
#define NV_PAPU_SECTL                                    0x00002000
#   define NV_PAPU_SECTL_XCNTMODE                           0x00000018
#       define NV_PAPU_SECTL_XCNTMODE_OFF                       0
#define NV_PAPU_VPVADDR                                  0x0000202C
#define NV_PAPU_TVL2D                                    0x00002054
#define NV_PAPU_CVL2D                                    0x00002058
#define NV_PAPU_NVL2D                                    0x0000205C
#define NV_PAPU_TVL3D                                    0x00002060
#define NV_PAPU_CVL3D                                    0x00002064
#define NV_PAPU_NVL3D                                    0x00002068
#define NV_PAPU_TVLMP                                    0x0000206C
#define NV_PAPU_CVLMP                                    0x00002070
#define NV_PAPU_NVLMP                                    0x00002074

static const struct {
    hwaddr top, current, next;
} voice_list_regs[] = {
    {NV_PAPU_TVL2D, NV_PAPU_CVL2D, NV_PAPU_NVL2D}, //2D
    {NV_PAPU_TVL3D, NV_PAPU_CVL3D, NV_PAPU_NVL3D}, //3D
    {NV_PAPU_TVLMP, NV_PAPU_CVLMP, NV_PAPU_NVLMP}, //MP
};


/* audio processor object / front-end messages */
#define NV1BA0_PIO_FREE                                  0x00000010
#define NV1BA0_PIO_SET_ANTECEDENT_VOICE                  0x00000120
#   define NV1BA0_PIO_SET_ANTECEDENT_VOICE_HANDLE           0x0000FFFF
#   define NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST             0x00030000
#       define NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_INHERIT     0
#       define NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP      1
#       define NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_3D_TOP      2
#       define NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_MP_TOP      3
#define NV1BA0_PIO_VOICE_ON                              0x00000124
#   define NV1BA0_PIO_VOICE_ON_HANDLE                       0x0000FFFF
#define NV1BA0_PIO_VOICE_OFF                             0x00000128
#define NV1BA0_PIO_SET_CURRENT_VOICE                     0x000002F8

#define SE2FE_IDLE_VOICE                                 0x00008000


/* voice structure */
#define NV_PAVS_SIZE                                     0x00000080
#define NV_PAVS_VOICE_PAR_STATE                          0x00000054
#   define NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE             (1 << 21)
#define NV_PAVS_VOICE_TAR_PITCH_LINK                     0x0000007c
#   define NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE   0x0000FFFF



#define MCPX_HW_MAX_VOICES 256


#define GET_MASK(v, mask) (((v) & (mask)) >> (ffs(mask)-1))

#define SET_MASK(v, mask, val)                                       \
    do {                                                             \
        (v) &= ~(mask);                                              \
        (v) |= ((val) << (ffs(mask)-1)) & (mask);                    \
    } while (0)



//#define DEBUG
#ifdef DEBUG
# define MCPX_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define MCPX_DPRINTF(format, ...)       do { } while (0)
#endif


typedef struct MCPXAPUState {
    PCIDevice dev;
    qemu_irq irq;

    MemoryRegion mmio;

    /* Setup Engine */
    struct {
        QEMUTimer *frame_timer;
    } se;

    /* Voice Processor */
    struct {
        MemoryRegion mmio;
    } vp;

    /* Global Processor */
    struct {
        MemoryRegion mmio;
    } gp;

    uint32_t regs[0x20000];

} MCPXAPUState;


#define MCPX_APU_DEVICE(obj) \
    OBJECT_CHECK(MCPXAPUState, (obj), "mcpx-apu")

static uint32_t voice_get_mask(MCPXAPUState *d,
                               unsigned int voice_handle,
                               hwaddr offset,
                               uint32_t mask)
{
    assert(voice_handle != 0xFFFF);
    hwaddr voice = d->regs[NV_PAPU_VPVADDR]
                    + voice_handle * NV_PAVS_SIZE;
    return (ldl_le_phys(voice + offset) & mask) >> (ffs(mask)-1);
}
static void voice_set_mask(MCPXAPUState *d,
                           unsigned int voice_handle,
                           hwaddr offset,
                           uint32_t mask,
                           uint32_t val)
{
    assert(voice_handle != 0xFFFF);
    hwaddr voice = d->regs[NV_PAPU_VPVADDR]
                    + voice_handle * NV_PAVS_SIZE;
    uint32_t v = ldl_le_phys(voice + offset) & ~mask;
    stl_le_phys(voice + offset,
                v | ((val << (ffs(mask)-1)) & mask));
}



static void update_irq(MCPXAPUState *d)
{
    if ((d->regs[NV_PAPU_IEN] & NV_PAPU_ISTS_GINTSTS)
        && ((d->regs[NV_PAPU_ISTS] & ~NV_PAPU_ISTS_GINTSTS)
              & d->regs[NV_PAPU_IEN])) {

        d->regs[NV_PAPU_ISTS] |= NV_PAPU_ISTS_GINTSTS;
        MCPX_DPRINTF("mcpx irq raise\n");
        qemu_irq_raise(d->irq);
    } else {
        d->regs[NV_PAPU_ISTS] &= ~NV_PAPU_ISTS_GINTSTS;
        MCPX_DPRINTF("mcpx irq lower\n");
        qemu_irq_lower(d->irq);
    }
}

static uint64_t mcpx_apu_read(void *opaque,
                              hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    uint64_t r = 0;
    switch (addr) {
    default:
        if (addr < 0x20000) {
            r = d->regs[addr];
        }
        break;
    }

    MCPX_DPRINTF("mcpx apu: read [0x%llx] -> 0x%llx\n", addr, r);
    return r;
}
static void mcpx_apu_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    MCPXAPUState *d = opaque;

    MCPX_DPRINTF("mcpx apu: [0x%llx] = 0x%llx\n", addr, val);

    switch (addr) {
    case NV_PAPU_ISTS:
        /* the bits of the interrupts to clear are wrtten */
        d->regs[NV_PAPU_ISTS] &= ~val;
        update_irq(d);
        break;
    case NV_PAPU_SECTL:
        if ( ((val & NV_PAPU_SECTL_XCNTMODE) >> 3)
                == NV_PAPU_SECTL_XCNTMODE_OFF) {
            qemu_del_timer(d->se.frame_timer);
        } else {
            qemu_mod_timer(d->se.frame_timer, qemu_get_clock_ms(vm_clock) + 10);
        }
        d->regs[addr] = val;
        break;
    case NV_PAPU_FEMEMDATA:
        /* 'magic write'
         * This value is expected to be written to FEMEMADDR on completion of
         * something to do with notifies. Just do it now :/ */
        stl_le_phys(d->regs[NV_PAPU_FEMEMADDR], val);
        d->regs[addr] = val;
        break;
    default:
        if (addr < 0x20000) {
            d->regs[addr] = val;
        }
        break;
    }
}
static const MemoryRegionOps mcpx_apu_mmio_ops = {
    .read = mcpx_apu_read,
    .write = mcpx_apu_write,
};


static void fe_method(MCPXAPUState *d,
                      uint32_t method, uint32_t argument)
{
    MCPX_DPRINTF("mcpx fe_method 0x%x 0x%x\n", method, argument);

    //assert((d->regs[NV_PAPU_FECTL] & NV_PAPU_FECTL_FEMETHMODE) == 0);

    d->regs[NV_PAPU_FEDECMETH] = method;
    d->regs[NV_PAPU_FEDECPARAM] = argument;
    unsigned int selected_handle, list;
    switch (method) {
    case NV1BA0_PIO_SET_ANTECEDENT_VOICE:
        d->regs[NV_PAPU_FEAV] = argument;
        break;
    case NV1BA0_PIO_VOICE_ON:
        selected_handle = argument & NV1BA0_PIO_VOICE_ON_HANDLE;
        list = GET_MASK(d->regs[NV_PAPU_FEAV], NV_PAPU_FEAV_LST);
        if (list != NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_INHERIT) {
            /* voice is added to the top of the selected list */
            unsigned int top_reg = voice_list_regs[list-1].top;
            voice_set_mask(d, selected_handle,
                NV_PAVS_VOICE_TAR_PITCH_LINK,
                NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,
                d->regs[top_reg]);
            d->regs[top_reg] = selected_handle;
        } else {
            unsigned int antecedent_voice =
                GET_MASK(d->regs[NV_PAPU_FEAV], NV_PAPU_FEAV_VALUE);
            /* voice is added after the antecedent voice */
            assert(antecedent_voice != 0xFFFF);

            uint32_t next_handle = voice_get_mask(d, antecedent_voice,
                NV_PAVS_VOICE_TAR_PITCH_LINK,
                NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);
            voice_set_mask(d, selected_handle,
                NV_PAVS_VOICE_TAR_PITCH_LINK,
                NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,
                next_handle);
            voice_set_mask(d, antecedent_voice,
                NV_PAVS_VOICE_TAR_PITCH_LINK,
                NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,
                selected_handle);

            voice_set_mask(d, selected_handle,
                    NV_PAVS_VOICE_PAR_STATE,
                    NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,
                    1);
        }
        break;
    case NV1BA0_PIO_VOICE_OFF:
        voice_set_mask(d, argument,
                NV_PAVS_VOICE_PAR_STATE,
                NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,
                0);
        break;
    case NV1BA0_PIO_SET_CURRENT_VOICE:
        d->regs[NV_PAPU_FECV] = argument;
        break;
    case SE2FE_IDLE_VOICE:
        if (d->regs[NV_PAPU_FETFORCE1] & NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE) {
            
            d->regs[NV_PAPU_FECTL] &= ~NV_PAPU_FECTL_FEMETHMODE;
            d->regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FEMETHMODE_TRAPPED;

            d->regs[NV_PAPU_FECTL] &= ~NV_PAPU_FECTL_FETRAPREASON;
            d->regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FETRAPREASON_REQUESTED;

            d->regs[NV_PAPU_ISTS] |= NV_PAPU_ISTS_FETINTSTS;
            update_irq(d);
        } else {
            assert(false);
        }
        break;
    default:
        assert(false);
        break;
    }
}


static uint64_t vp_read(void *opaque,
                        hwaddr addr, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu VP: read [0x%llx]\n", addr);
    switch (addr) {
    case NV1BA0_PIO_FREE:
        /* we don't simulate the queue for now,
         * pretend to always be empty */
        return 0x80;
    default:
        break;
    }
    return 0;
}
static void vp_write(void *opaque, hwaddr addr,
                     uint64_t val, unsigned int size)
{
    MCPXAPUState *d = opaque;

    MCPX_DPRINTF("mcpx apu VP: [0x%llx] = 0x%llx\n", addr, val);

    switch (addr) {
    case NV1BA0_PIO_SET_ANTECEDENT_VOICE:
    case NV1BA0_PIO_VOICE_ON:
    case NV1BA0_PIO_VOICE_OFF:
    case NV1BA0_PIO_SET_CURRENT_VOICE:
        /* TODO: these should instead be queueing up fe commands */
        fe_method(d, addr, val);
        break;
    default:
        break;
    }
}
static const MemoryRegionOps vp_ops = {
    .read = vp_read,
    .write = vp_write,
};


/* Global Processor - programmable DSP */
static uint64_t gp_read(void *opaque,
                        hwaddr addr, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu GP: read [0x%llx]\n", addr);
    return 0;
}
static void gp_write(void *opaque, hwaddr addr,
                     uint64_t val, unsigned int size)
{
    MCPX_DPRINTF("mcpx apu GP: [0x%llx] = 0x%llx\n", addr, val);
}
static const MemoryRegionOps gp_ops = {
    .read = gp_read,
    .write = gp_write,
};


/* TODO: this should be on a thread so it waits on the voice lock */
static void se_frame(void *opaque)
{
    MCPXAPUState *d = opaque;
    qemu_mod_timer(d->se.frame_timer, qemu_get_clock_ms(vm_clock) + 10);
    MCPX_DPRINTF("mcpx frame ping\n");
    int list;
    for (list=0; list < 3; list++) {
        hwaddr top, current, next;
        top = voice_list_regs[list].top;
        current = voice_list_regs[list].current;
        next = voice_list_regs[list].next;

        d->regs[current] = d->regs[top];
        while (d->regs[current] != 0xFFFF) {
            d->regs[next] = voice_get_mask(d, d->regs[current],
                NV_PAVS_VOICE_TAR_PITCH_LINK,
                NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);
            if (!voice_get_mask(d, d->regs[current],
                    NV_PAVS_VOICE_PAR_STATE,
                    NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE)) {
                MCPX_DPRINTF("voice %d not active...!\n", d->regs[current]);
                fe_method(d, SE2FE_IDLE_VOICE, d->regs[current]);
            }
            d->regs[current] = d->regs[next];
        }
    }
}


static int mcpx_apu_initfn(PCIDevice *dev)
{
    MCPXAPUState *d = MCPX_APU_DEVICE(dev);

    memory_region_init_io(&d->mmio, OBJECT(dev), &mcpx_apu_mmio_ops, d,
                          "mcpx-apu-mmio", 0x80000);

    memory_region_init_io(&d->vp.mmio, OBJECT(dev), &vp_ops, d,
                          "mcpx-apu-vp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x20000, &d->vp.mmio);

    memory_region_init_io(&d->gp.mmio, OBJECT(dev), &gp_ops, d,
                          "mcpx-apu-gp", 0x10000);
    memory_region_add_subregion(&d->mmio, 0x30000, &d->gp.mmio);

    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);


    d->se.frame_timer = qemu_new_timer_ms(vm_clock, se_frame, d);

    return 0;
}

static void mcpx_apu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_MCPX_APU;
    k->revision = 210;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    k->init = mcpx_apu_initfn;

    dc->desc = "MCPX Audio Processing Unit";
}

static const TypeInfo mcpx_apu_info = {
    .name          = "mcpx-apu",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MCPXAPUState),
    .class_init    = mcpx_apu_class_init,
};

static void mcpx_apu_register(void)
{
    type_register_static(&mcpx_apu_info);
}
type_init(mcpx_apu_register);



void mcpx_apu_init(PCIBus *bus, int devfn, qemu_irq irq)
{
    PCIDevice *dev;
    MCPXAPUState *d;
    dev = pci_create_simple(bus, devfn, "mcpx-apu");
    d = MCPX_APU_DEVICE(dev);
    d->irq = irq;
}