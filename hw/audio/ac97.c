/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "hw/hw.h"
#include "hw/audio/audio.h"
#include "audio/audio.h"
#include "hw/pci/pci.h"
#include "sysemu/dma.h"

enum {
    AC97_Reset                     = 0x00,
    AC97_Master_Volume_Mute        = 0x02,
    AC97_Headphone_Volume_Mute     = 0x04,
    AC97_Master_Volume_Mono_Mute   = 0x06,
    AC97_Master_Tone_RL            = 0x08,
    AC97_PC_BEEP_Volume_Mute       = 0x0A,
    AC97_Phone_Volume_Mute         = 0x0C,
    AC97_Mic_Volume_Mute           = 0x0E,
    AC97_Line_In_Volume_Mute       = 0x10,
    AC97_CD_Volume_Mute            = 0x12,
    AC97_Video_Volume_Mute         = 0x14,
    AC97_Aux_Volume_Mute           = 0x16,
    AC97_PCM_Out_Volume_Mute       = 0x18,
    AC97_Record_Select             = 0x1A,
    AC97_Record_Gain_Mute          = 0x1C,
    AC97_Record_Gain_Mic_Mute      = 0x1E,
    AC97_General_Purpose           = 0x20,
    AC97_3D_Control                = 0x22,
    AC97_AC_97_RESERVED            = 0x24,
    AC97_Powerdown_Ctrl_Stat       = 0x26,
    AC97_Extended_Audio_ID         = 0x28,
    AC97_Extended_Audio_Ctrl_Stat  = 0x2A,
    AC97_PCM_Front_DAC_Rate        = 0x2C,
    AC97_PCM_Surround_DAC_Rate     = 0x2E,
    AC97_PCM_LFE_DAC_Rate          = 0x30,
    AC97_PCM_LR_ADC_Rate           = 0x32,
    AC97_MIC_ADC_Rate              = 0x34,
    AC97_6Ch_Vol_C_LFE_Mute        = 0x36,
    AC97_6Ch_Vol_L_R_Surround_Mute = 0x38,
    AC97_Vendor_Reserved           = 0x58,
    AC97_Sigmatel_Analog           = 0x6c, /* We emulate a Sigmatel codec */
    AC97_Sigmatel_Dac2Invert       = 0x6e, /* We emulate a Sigmatel codec */
    AC97_Vendor_ID1                = 0x7c,
    AC97_Vendor_ID2                = 0x7e
};

#define SOFT_VOLUME
#define SR_FIFOE 16             /* rwc */
#define SR_BCIS  8              /* rwc */
#define SR_LVBCI 4              /* rwc */
#define SR_CELV  2              /* ro */
#define SR_DCH   1              /* ro */
#define SR_VALID_MASK ((1 << 5) - 1)
#define SR_WCLEAR_MASK (SR_FIFOE | SR_BCIS | SR_LVBCI)
#define SR_RO_MASK (SR_DCH | SR_CELV)
#define SR_INT_MASK (SR_FIFOE | SR_BCIS | SR_LVBCI)

#define CR_IOCE  16             /* rw */
#define CR_FEIE  8              /* rw */
#define CR_LVBIE 4              /* rw */
#define CR_RR    2              /* rw */
#define CR_RPBM  1              /* rw */
#define CR_VALID_MASK ((1 << 5) - 1)
#define CR_DONT_CLEAR_MASK (CR_IOCE | CR_FEIE | CR_LVBIE)

#define GC_WR    4              /* rw */
#define GC_CR    2              /* rw */
#define GC_VALID_MASK ((1 << 6) - 1)

#define GS_MD3   (1<<17)        /* rw */
#define GS_AD3   (1<<16)        /* rw */
#define GS_RCS   (1<<15)        /* rwc */
#define GS_B3S12 (1<<14)        /* ro */
#define GS_B2S12 (1<<13)        /* ro */
#define GS_B1S12 (1<<12)        /* ro */
#define GS_S1R1  (1<<11)        /* rwc */
#define GS_S0R1  (1<<10)        /* rwc */
#define GS_S1CR  (1<<9)         /* ro */
#define GS_S0CR  (1<<8)         /* ro */
#define GS_MINT  (1<<7)         /* ro */
#define GS_POINT (1<<6)         /* ro */
#define GS_PIINT (1<<5)         /* ro */
#define GS_RSRVD ((1<<4)|(1<<3))
#define GS_MOINT (1<<2)         /* ro */
#define GS_MIINT (1<<1)         /* ro */
#define GS_GSCI  1              /* rwc */
#define GS_RO_MASK (GS_B3S12|                   \
                    GS_B2S12|                   \
                    GS_B1S12|                   \
                    GS_S1CR|                    \
                    GS_S0CR|                    \
                    GS_MINT|                    \
                    GS_POINT|                   \
                    GS_PIINT|                   \
                    GS_RSRVD|                   \
                    GS_MOINT|                   \
                    GS_MIINT)
#define GS_VALID_MASK ((1 << 18) - 1)
#define GS_WCLEAR_MASK (GS_RCS|GS_S1R1|GS_S0R1|GS_GSCI)

#define BD_IOC (1<<31)
#define BD_BUP (1<<30)

#define EACS_VRA 1
#define EACS_VRM 8

#define MUTE_SHIFT 15

#define REC_MASK 7
enum {
    REC_MIC = 0,
    REC_CD,
    REC_VIDEO,
    REC_AUX,
    REC_LINE_IN,
    REC_STEREO_MIX,
    REC_MONO_MIX,
    REC_PHONE
};

typedef struct BD {
    uint32_t addr;
    uint32_t ctl_len;
} BD;

typedef struct AC97BusMasterRegs {
    uint32_t bdbar;             /* rw 0 */
    uint8_t civ;                /* ro 0 */
    uint8_t lvi;                /* rw 0 */
    uint16_t sr;                /* rw 1 */
    uint16_t picb;              /* ro 0 */
    uint8_t piv;                /* ro 0 */
    uint8_t cr;                 /* rw 0 */
    unsigned int bd_valid;
    BD bd;
} AC97BusMasterRegs;

typedef struct AC97LinkState {
    PCIDevice dev;
    QEMUSoundCard card;
    uint32_t use_broken_id;
    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint32_t cas;
    uint32_t last_samp;
    AC97BusMasterRegs bm_regs[3];
    uint8_t mixer_data[256];
    SWVoiceIn *voice_pi;
    SWVoiceOut *voice_po;
    SWVoiceIn *voice_mc;
    int invalid_freq[3];
    uint8_t silence[128];
    int bup_flag;
    MemoryRegion io_nam;
    MemoryRegion io_nabm;
} AC97LinkState;

enum {
    BUP_SET = 1,
    BUP_LAST = 2
};

#ifdef DEBUG_AC97
#define dolog(...) AUD_log ("ac97", __VA_ARGS__)
#else
#define dolog(...)
#endif

#define MKREGS(prefix, start)                   \
enum {                                          \
    prefix ## _BDBAR = start,                   \
    prefix ## _CIV = start + 4,                 \
    prefix ## _LVI = start + 5,                 \
    prefix ## _SR = start + 6,                  \
    prefix ## _PICB = start + 8,                \
    prefix ## _PIV = start + 10,                \
    prefix ## _CR = start + 11                  \
}

enum {
    PI_INDEX = 0,
    PO_INDEX,
    MC_INDEX,
    LAST_INDEX
};

MKREGS (PI, PI_INDEX * 16);
MKREGS (PO, PO_INDEX * 16);
MKREGS (MC, MC_INDEX * 16);

enum {
    GLOB_CNT = 0x2c,
    GLOB_STA = 0x30,
    CAS      = 0x34
};

#define GET_BM(index) (((index) >> 4) & 3)

static void po_callback (void *opaque, int free);
static void pi_callback (void *opaque, int avail);
static void mc_callback (void *opaque, int avail);

static void warm_reset (AC97LinkState *s)
{
    (void) s;
}

static void cold_reset (AC97LinkState * s)
{
    (void) s;
}

static void fetch_bd (AC97LinkState *s, AC97BusMasterRegs *r)
{
    uint8_t b[8];

    pci_dma_read (&s->dev, r->bdbar + r->civ * 8, b, 8);
    r->bd_valid = 1;
    r->bd.addr = le32_to_cpu (*(uint32_t *) &b[0]) & ~3;
    r->bd.ctl_len = le32_to_cpu (*(uint32_t *) &b[4]);
    r->picb = r->bd.ctl_len & 0xffff;
    dolog ("bd %2d addr=%#x ctl=%#06x len=%#x(%d bytes)\n",
           r->civ, r->bd.addr, r->bd.ctl_len >> 16,
           r->bd.ctl_len & 0xffff,
           (r->bd.ctl_len & 0xffff) << 1);
}

static void update_sr (AC97LinkState *s, AC97BusMasterRegs *r, uint32_t new_sr)
{
    int event = 0;
    int level = 0;
    uint32_t new_mask = new_sr & SR_INT_MASK;
    uint32_t old_mask = r->sr & SR_INT_MASK;
    uint32_t masks[] = {GS_PIINT, GS_POINT, GS_MINT};

    if (new_mask ^ old_mask) {
        /** @todo is IRQ deasserted when only one of status bits is cleared? */
        if (!new_mask) {
            event = 1;
            level = 0;
        }
        else {
            if ((new_mask & SR_LVBCI) && (r->cr & CR_LVBIE)) {
                event = 1;
                level = 1;
            }
            if ((new_mask & SR_BCIS) && (r->cr & CR_IOCE)) {
                event = 1;
                level = 1;
            }
        }
    }

    r->sr = new_sr;

    dolog ("IOC%d LVB%d sr=%#x event=%d level=%d\n",
           r->sr & SR_BCIS, r->sr & SR_LVBCI,
           r->sr,
           event, level);

    if (!event)
        return;

    if (level) {
        s->glob_sta |= masks[r - s->bm_regs];
        dolog ("set irq level=1\n");
        pci_irq_assert(&s->dev);
    }
    else {
        s->glob_sta &= ~masks[r - s->bm_regs];
        dolog ("set irq level=0\n");
        pci_irq_deassert(&s->dev);
    }
}

static void voice_set_active (AC97LinkState *s, int bm_index, int on)
{
    switch (bm_index) {
    case PI_INDEX:
        AUD_set_active_in (s->voice_pi, on);
        break;

    case PO_INDEX:
        AUD_set_active_out (s->voice_po, on);
        break;

    case MC_INDEX:
        AUD_set_active_in (s->voice_mc, on);
        break;

    default:
        AUD_log ("ac97", "invalid bm_index(%d) in voice_set_active", bm_index);
        break;
    }
}

static void reset_bm_regs (AC97LinkState *s, AC97BusMasterRegs *r)
{
    dolog ("reset_bm_regs\n");
    r->bdbar = 0;
    r->civ = 0;
    r->lvi = 0;
    /** todo do we need to do that? */
    update_sr (s, r, SR_DCH);
    r->picb = 0;
    r->piv = 0;
    r->cr = r->cr & CR_DONT_CLEAR_MASK;
    r->bd_valid = 0;

    voice_set_active (s, r - s->bm_regs, 0);
    memset (s->silence, 0, sizeof (s->silence));
}

static void mixer_store (AC97LinkState *s, uint32_t i, uint16_t v)
{
    if (i + 2 > sizeof (s->mixer_data)) {
        dolog ("mixer_store: index %d out of bounds %zd\n",
               i, sizeof (s->mixer_data));
        return;
    }

    s->mixer_data[i + 0] = v & 0xff;
    s->mixer_data[i + 1] = v >> 8;
}

static uint16_t mixer_load (AC97LinkState *s, uint32_t i)
{
    uint16_t val = 0xffff;

    if (i + 2 > sizeof (s->mixer_data)) {
        dolog ("mixer_load: index %d out of bounds %zd\n",
               i, sizeof (s->mixer_data));
    }
    else {
        val = s->mixer_data[i + 0] | (s->mixer_data[i + 1] << 8);
    }

    return val;
}

static void open_voice (AC97LinkState *s, int index, int freq)
{
    struct audsettings as;

    as.freq = freq;
    as.nchannels = 2;
    as.fmt = AUD_FMT_S16;
    as.endianness = 0;

    if (freq > 0) {
        s->invalid_freq[index] = 0;
        switch (index) {
        case PI_INDEX:
            s->voice_pi = AUD_open_in (
                &s->card,
                s->voice_pi,
                "ac97.pi",
                s,
                pi_callback,
                &as
                );
            break;

        case PO_INDEX:
            s->voice_po = AUD_open_out (
                &s->card,
                s->voice_po,
                "ac97.po",
                s,
                po_callback,
                &as
                );
            break;

        case MC_INDEX:
            s->voice_mc = AUD_open_in (
                &s->card,
                s->voice_mc,
                "ac97.mc",
                s,
                mc_callback,
                &as
                );
            break;
        }
    }
    else {
        s->invalid_freq[index] = freq;
        switch (index) {
        case PI_INDEX:
            AUD_close_in (&s->card, s->voice_pi);
            s->voice_pi = NULL;
            break;

        case PO_INDEX:
            AUD_close_out (&s->card, s->voice_po);
            s->voice_po = NULL;
            break;

        case MC_INDEX:
            AUD_close_in (&s->card, s->voice_mc);
            s->voice_mc = NULL;
            break;
        }
    }
}

static void reset_voices (AC97LinkState *s, uint8_t active[LAST_INDEX])
{
    uint16_t freq;

    freq = mixer_load (s, AC97_PCM_LR_ADC_Rate);
    open_voice (s, PI_INDEX, freq);
    AUD_set_active_in (s->voice_pi, active[PI_INDEX]);

    freq = mixer_load (s, AC97_PCM_Front_DAC_Rate);
    open_voice (s, PO_INDEX, freq);
    AUD_set_active_out (s->voice_po, active[PO_INDEX]);

    freq = mixer_load (s, AC97_MIC_ADC_Rate);
    open_voice (s, MC_INDEX, freq);
    AUD_set_active_in (s->voice_mc, active[MC_INDEX]);
}

static void get_volume (uint16_t vol, uint16_t mask, int inverse,
                        int *mute, uint8_t *lvol, uint8_t *rvol)
{
    *mute = (vol >> MUTE_SHIFT) & 1;
    *rvol = (255 * (vol & mask)) / mask;
    *lvol = (255 * ((vol >> 8) & mask)) / mask;

    if (inverse) {
        *rvol = 255 - *rvol;
        *lvol = 255 - *lvol;
    }
}

static void update_combined_volume_out (AC97LinkState *s)
{
    uint8_t lvol, rvol, plvol, prvol;
    int mute, pmute;

    get_volume (mixer_load (s, AC97_Master_Volume_Mute), 0x3f, 1,
                &mute, &lvol, &rvol);
    get_volume (mixer_load (s, AC97_PCM_Out_Volume_Mute), 0x1f, 1,
                &pmute, &plvol, &prvol);

    mute = mute | pmute;
    lvol = (lvol * plvol) / 255;
    rvol = (rvol * prvol) / 255;

    AUD_set_volume_out (s->voice_po, mute, lvol, rvol);
}

static void update_volume_in (AC97LinkState *s)
{
    uint8_t lvol, rvol;
    int mute;

    get_volume (mixer_load (s, AC97_Record_Gain_Mute), 0x0f, 0,
                &mute, &lvol, &rvol);

    AUD_set_volume_in (s->voice_pi, mute, lvol, rvol);
}

static void set_volume (AC97LinkState *s, int index, uint32_t val)
{
    switch (index) {
    case AC97_Master_Volume_Mute:
        val &= 0xbf3f;
        mixer_store (s, index, val);
        update_combined_volume_out (s);
        break;
    case AC97_PCM_Out_Volume_Mute:
        val &= 0x9f1f;
        mixer_store (s, index, val);
        update_combined_volume_out (s);
        break;
    case AC97_Record_Gain_Mute:
        val &= 0x8f0f;
        mixer_store (s, index, val);
        update_volume_in (s);
        break;
    }
}

static void record_select (AC97LinkState *s, uint32_t val)
{
    uint8_t rs = val & REC_MASK;
    uint8_t ls = (val >> 8) & REC_MASK;
    mixer_store (s, AC97_Record_Select, rs | (ls << 8));
}

static void mixer_reset (AC97LinkState *s)
{
    uint8_t active[LAST_INDEX];

    dolog ("mixer_reset\n");
    memset (s->mixer_data, 0, sizeof (s->mixer_data));
    memset (active, 0, sizeof (active));
    mixer_store (s, AC97_Reset                   , 0x0000); /* 6940 */
    mixer_store (s, AC97_Headphone_Volume_Mute   , 0x0000);
    mixer_store (s, AC97_Master_Volume_Mono_Mute , 0x0000);
    mixer_store (s, AC97_Master_Tone_RL,           0x0000);
    mixer_store (s, AC97_PC_BEEP_Volume_Mute     , 0x0000);
    mixer_store (s, AC97_Phone_Volume_Mute       , 0x0000);
    mixer_store (s, AC97_Mic_Volume_Mute         , 0x0000);
    mixer_store (s, AC97_Line_In_Volume_Mute     , 0x0000);
    mixer_store (s, AC97_CD_Volume_Mute          , 0x0000);
    mixer_store (s, AC97_Video_Volume_Mute       , 0x0000);
    mixer_store (s, AC97_Aux_Volume_Mute         , 0x0000);
    mixer_store (s, AC97_Record_Gain_Mic_Mute    , 0x0000);
    mixer_store (s, AC97_General_Purpose         , 0x0000);
    mixer_store (s, AC97_3D_Control              , 0x0000);
    mixer_store (s, AC97_Powerdown_Ctrl_Stat     , 0x000f);

    /*
     * Sigmatel 9700 (STAC9700)
     */
    mixer_store (s, AC97_Vendor_ID1              , 0x8384);
    mixer_store (s, AC97_Vendor_ID2              , 0x7600); /* 7608 */

    mixer_store (s, AC97_Extended_Audio_ID       , 0x0809);
    mixer_store (s, AC97_Extended_Audio_Ctrl_Stat, 0x0009);
    mixer_store (s, AC97_PCM_Front_DAC_Rate      , 0xbb80);
    mixer_store (s, AC97_PCM_Surround_DAC_Rate   , 0xbb80);
    mixer_store (s, AC97_PCM_LFE_DAC_Rate        , 0xbb80);
    mixer_store (s, AC97_PCM_LR_ADC_Rate         , 0xbb80);
    mixer_store (s, AC97_MIC_ADC_Rate            , 0xbb80);

    record_select (s, 0);
    set_volume (s, AC97_Master_Volume_Mute, 0x8000);
    set_volume (s, AC97_PCM_Out_Volume_Mute, 0x8808);
    set_volume (s, AC97_Record_Gain_Mute, 0x8808);

    reset_voices (s, active);
}

/**
 * Native audio mixer
 * I/O Reads
 */
static uint32_t nam_readb (void *opaque, uint32_t addr)
{
    AC97LinkState *s = opaque;
    dolog ("U nam readb %#x\n", addr);
    s->cas = 0;
    return ~0U;
}

static uint32_t nam_readw (void *opaque, uint32_t addr)
{
    AC97LinkState *s = opaque;
    uint32_t val = ~0U;
    uint32_t index = addr;
    s->cas = 0;
    val = mixer_load (s, index);
    return val;
}

static uint32_t nam_readl (void *opaque, uint32_t addr)
{
    AC97LinkState *s = opaque;
    dolog ("U nam readl %#x\n", addr);
    s->cas = 0;
    return ~0U;
}

/**
 * Native audio mixer
 * I/O Writes
 */
static void nam_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    AC97LinkState *s = opaque;
    dolog ("U nam writeb %#x <- %#x\n", addr, val);
    s->cas = 0;
}

static void nam_writew (void *opaque, uint32_t addr, uint32_t val)
{
    AC97LinkState *s = opaque;
    uint32_t index = addr;
    s->cas = 0;
    switch (index) {
    case AC97_Reset:
        mixer_reset (s);
        break;
    case AC97_Powerdown_Ctrl_Stat:
        val &= ~0x800f;
        val |= mixer_load (s, index) & 0xf;
        mixer_store (s, index, val);
        break;
    case AC97_PCM_Out_Volume_Mute:
    case AC97_Master_Volume_Mute:
    case AC97_Record_Gain_Mute:
        set_volume (s, index, val);
        break;
    case AC97_Record_Select:
        record_select (s, val);
        break;
    case AC97_Vendor_ID1:
    case AC97_Vendor_ID2:
        dolog ("Attempt to write vendor ID to %#x\n", val);
        break;
    case AC97_Extended_Audio_ID:
        dolog ("Attempt to write extended audio ID to %#x\n", val);
        break;
    case AC97_Extended_Audio_Ctrl_Stat:
        if (!(val & EACS_VRA)) {
            mixer_store (s, AC97_PCM_Front_DAC_Rate, 0xbb80);
            mixer_store (s, AC97_PCM_LR_ADC_Rate,    0xbb80);
            open_voice (s, PI_INDEX, 48000);
            open_voice (s, PO_INDEX, 48000);
        }
        if (!(val & EACS_VRM)) {
            mixer_store (s, AC97_MIC_ADC_Rate, 0xbb80);
            open_voice (s, MC_INDEX, 48000);
        }
        dolog ("Setting extended audio control to %#x\n", val);
        mixer_store (s, AC97_Extended_Audio_Ctrl_Stat, val);
        break;
    case AC97_PCM_Front_DAC_Rate:
        if (mixer_load (s, AC97_Extended_Audio_Ctrl_Stat) & EACS_VRA) {
            mixer_store (s, index, val);
            dolog ("Set front DAC rate to %d\n", val);
            open_voice (s, PO_INDEX, val);
        }
        else {
            dolog ("Attempt to set front DAC rate to %d, "
                   "but VRA is not set\n",
                   val);
        }
        break;
    case AC97_MIC_ADC_Rate:
        if (mixer_load (s, AC97_Extended_Audio_Ctrl_Stat) & EACS_VRM) {
            mixer_store (s, index, val);
            dolog ("Set MIC ADC rate to %d\n", val);
            open_voice (s, MC_INDEX, val);
        }
        else {
            dolog ("Attempt to set MIC ADC rate to %d, "
                   "but VRM is not set\n",
                   val);
        }
        break;
    case AC97_PCM_LR_ADC_Rate:
        if (mixer_load (s, AC97_Extended_Audio_Ctrl_Stat) & EACS_VRA) {
            mixer_store (s, index, val);
            dolog ("Set front LR ADC rate to %d\n", val);
            open_voice (s, PI_INDEX, val);
        }
        else {
            dolog ("Attempt to set LR ADC rate to %d, but VRA is not set\n",
                    val);
        }
        break;
    case AC97_Headphone_Volume_Mute:
    case AC97_Master_Volume_Mono_Mute:
    case AC97_Master_Tone_RL:
    case AC97_PC_BEEP_Volume_Mute:
    case AC97_Phone_Volume_Mute:
    case AC97_Mic_Volume_Mute:
    case AC97_Line_In_Volume_Mute:
    case AC97_CD_Volume_Mute:
    case AC97_Video_Volume_Mute:
    case AC97_Aux_Volume_Mute:
    case AC97_Record_Gain_Mic_Mute:
    case AC97_General_Purpose:
    case AC97_3D_Control:
    case AC97_Sigmatel_Analog:
    case AC97_Sigmatel_Dac2Invert:
        /* None of the features in these regs are emulated, so they are RO */
        break;
    default:
        dolog ("U nam writew %#x <- %#x\n", addr, val);
        mixer_store (s, index, val);
        break;
    }
}

static void nam_writel (void *opaque, uint32_t addr, uint32_t val)
{
    AC97LinkState *s = opaque;
    dolog ("U nam writel %#x <- %#x\n", addr, val);
    s->cas = 0;
}

/**
 * Native audio bus master
 * I/O Reads
 */
static uint32_t nabm_readb (void *opaque, uint32_t addr)
{
    AC97LinkState *s = opaque;
    AC97BusMasterRegs *r = NULL;
    uint32_t index = addr;
    uint32_t val = ~0U;

    switch (index) {
    case CAS:
        dolog ("CAS %d\n", s->cas);
        val = s->cas;
        s->cas = 1;
        break;
    case PI_CIV:
    case PO_CIV:
    case MC_CIV:
        r = &s->bm_regs[GET_BM (index)];
        val = r->civ;
        dolog ("CIV[%d] -> %#x\n", GET_BM (index), val);
        break;
    case PI_LVI:
    case PO_LVI:
    case MC_LVI:
        r = &s->bm_regs[GET_BM (index)];
        val = r->lvi;
        dolog ("LVI[%d] -> %#x\n", GET_BM (index), val);
        break;
    case PI_PIV:
    case PO_PIV:
    case MC_PIV:
        r = &s->bm_regs[GET_BM (index)];
        val = r->piv;
        dolog ("PIV[%d] -> %#x\n", GET_BM (index), val);
        break;
    case PI_CR:
    case PO_CR:
    case MC_CR:
        r = &s->bm_regs[GET_BM (index)];
        val = r->cr;
        dolog ("CR[%d] -> %#x\n", GET_BM (index), val);
        break;
    case PI_SR:
    case PO_SR:
    case MC_SR:
        r = &s->bm_regs[GET_BM (index)];
        val = r->sr & 0xff;
        dolog ("SRb[%d] -> %#x\n", GET_BM (index), val);
        break;
    default:
        dolog ("U nabm readb %#x -> %#x\n", addr, val);
        break;
    }
    return val;
}

static uint32_t nabm_readw (void *opaque, uint32_t addr)
{
    AC97LinkState *s = opaque;
    AC97BusMasterRegs *r = NULL;
    uint32_t index = addr;
    uint32_t val = ~0U;

    switch (index) {
    case PI_SR:
    case PO_SR:
    case MC_SR:
        r = &s->bm_regs[GET_BM (index)];
        val = r->sr;
        dolog ("SR[%d] -> %#x\n", GET_BM (index), val);
        break;
    case PI_PICB:
    case PO_PICB:
    case MC_PICB:
        r = &s->bm_regs[GET_BM (index)];
        val = r->picb;
        dolog ("PICB[%d] -> %#x\n", GET_BM (index), val);
        break;
    default:
        dolog ("U nabm readw %#x -> %#x\n", addr, val);
        break;
    }
    return val;
}

static uint32_t nabm_readl (void *opaque, uint32_t addr)
{
    AC97LinkState *s = opaque;
    AC97BusMasterRegs *r = NULL;
    uint32_t index = addr;
    uint32_t val = ~0U;

    switch (index) {
    case PI_BDBAR:
    case PO_BDBAR:
    case MC_BDBAR:
        r = &s->bm_regs[GET_BM (index)];
        val = r->bdbar;
        dolog ("BMADDR[%d] -> %#x\n", GET_BM (index), val);
        break;
    case PI_CIV:
    case PO_CIV:
    case MC_CIV:
        r = &s->bm_regs[GET_BM (index)];
        val = r->civ | (r->lvi << 8) | (r->sr << 16);
        dolog ("CIV LVI SR[%d] -> %#x, %#x, %#x\n", GET_BM (index),
               r->civ, r->lvi, r->sr);
        break;
    case PI_PICB:
    case PO_PICB:
    case MC_PICB:
        r = &s->bm_regs[GET_BM (index)];
        val = r->picb | (r->piv << 16) | (r->cr << 24);
        dolog ("PICB PIV CR[%d] -> %#x %#x %#x %#x\n", GET_BM (index),
               val, r->picb, r->piv, r->cr);
        break;
    case GLOB_CNT:
        val = s->glob_cnt;
        dolog ("glob_cnt -> %#x\n", val);
        break;
    case GLOB_STA:
        val = s->glob_sta | GS_S0CR;
        dolog ("glob_sta -> %#x\n", val);
        break;
    default:
        dolog ("U nabm readl %#x -> %#x\n", addr, val);
        break;
    }
    return val;
}

/**
 * Native audio bus master
 * I/O Writes
 */
static void nabm_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    AC97LinkState *s = opaque;
    AC97BusMasterRegs *r = NULL;
    uint32_t index = addr;
    switch (index) {
    case PI_LVI:
    case PO_LVI:
    case MC_LVI:
        r = &s->bm_regs[GET_BM (index)];
        if ((r->cr & CR_RPBM) && (r->sr & SR_DCH)) {
            r->sr &= ~(SR_DCH | SR_CELV);
            r->civ = r->piv;
            r->piv = (r->piv + 1) % 32;
            fetch_bd (s, r);
        }
        r->lvi = val % 32;
        dolog ("LVI[%d] <- %#x\n", GET_BM (index), val);
        break;
    case PI_CR:
    case PO_CR:
    case MC_CR:
        r = &s->bm_regs[GET_BM (index)];
        if (val & CR_RR) {
            reset_bm_regs (s, r);
        }
        else {
            r->cr = val & CR_VALID_MASK;
            if (!(r->cr & CR_RPBM)) {
                voice_set_active (s, r - s->bm_regs, 0);
                r->sr |= SR_DCH;
            }
            else {
                r->civ = r->piv;
                r->piv = (r->piv + 1) % 32;
                fetch_bd (s, r);
                r->sr &= ~SR_DCH;
                voice_set_active (s, r - s->bm_regs, 1);
            }
        }
        dolog ("CR[%d] <- %#x (cr %#x)\n", GET_BM (index), val, r->cr);
        break;
    case PI_SR:
    case PO_SR:
    case MC_SR:
        r = &s->bm_regs[GET_BM (index)];
        r->sr |= val & ~(SR_RO_MASK | SR_WCLEAR_MASK);
        update_sr (s, r, r->sr & ~(val & SR_WCLEAR_MASK));
        dolog ("SR[%d] <- %#x (sr %#x)\n", GET_BM (index), val, r->sr);
        break;
    default:
        dolog ("U nabm writeb %#x <- %#x\n", addr, val);
        break;
    }
}

static void nabm_writew (void *opaque, uint32_t addr, uint32_t val)
{
    AC97LinkState *s = opaque;
    AC97BusMasterRegs *r = NULL;
    uint32_t index = addr;
    switch (index) {
    case PI_SR:
    case PO_SR:
    case MC_SR:
        r = &s->bm_regs[GET_BM (index)];
        r->sr |= val & ~(SR_RO_MASK | SR_WCLEAR_MASK);
        update_sr (s, r, r->sr & ~(val & SR_WCLEAR_MASK));
        dolog ("SR[%d] <- %#x (sr %#x)\n", GET_BM (index), val, r->sr);
        break;
    default:
        dolog ("U nabm writew %#x <- %#x\n", addr, val);
        break;
    }
}

static void nabm_writel (void *opaque, uint32_t addr, uint32_t val)
{
    AC97LinkState *s = opaque;
    AC97BusMasterRegs *r = NULL;
    uint32_t index = addr;
    switch (index) {
    case PI_BDBAR:
    case PO_BDBAR:
    case MC_BDBAR:
        r = &s->bm_regs[GET_BM (index)];
        r->bdbar = val & ~3;
        dolog ("BDBAR[%d] <- %#x (bdbar %#x)\n",
               GET_BM (index), val, r->bdbar);
        break;
    case GLOB_CNT:
        if (val & GC_WR)
            warm_reset (s);
        if (val & GC_CR)
            cold_reset (s);
        if (!(val & (GC_WR | GC_CR)))
            s->glob_cnt = val & GC_VALID_MASK;
        dolog ("glob_cnt <- %#x (glob_cnt %#x)\n", val, s->glob_cnt);
        break;
    case GLOB_STA:
        s->glob_sta &= ~(val & GS_WCLEAR_MASK);
        s->glob_sta |= (val & ~(GS_WCLEAR_MASK | GS_RO_MASK)) & GS_VALID_MASK;
        dolog ("glob_sta <- %#x (glob_sta %#x)\n", val, s->glob_sta);
        break;
    default:
        dolog ("U nabm writel %#x <- %#x\n", addr, val);
        break;
    }
}

static int write_audio (AC97LinkState *s, AC97BusMasterRegs *r,
                        int max, int *stop)
{
    uint8_t tmpbuf[4096];
    uint32_t addr = r->bd.addr;
    uint32_t temp = r->picb << 1;
    uint32_t written = 0;
    int to_copy = 0;
    temp = audio_MIN (temp, max);

    if (!temp) {
        *stop = 1;
        return 0;
    }

    while (temp) {
        int copied;
        to_copy = audio_MIN (temp, sizeof (tmpbuf));
        pci_dma_read (&s->dev, addr, tmpbuf, to_copy);
        copied = AUD_write (s->voice_po, tmpbuf, to_copy);
        dolog ("write_audio max=%x to_copy=%x copied=%x\n",
               max, to_copy, copied);
        if (!copied) {
            *stop = 1;
            break;
        }
        temp -= copied;
        addr += copied;
        written += copied;
    }

    if (!temp) {
        if (to_copy < 4) {
            dolog ("whoops\n");
            s->last_samp = 0;
        }
        else {
            s->last_samp = *(uint32_t *) &tmpbuf[to_copy - 4];
        }
    }

    r->bd.addr = addr;
    return written;
}

static void write_bup (AC97LinkState *s, int elapsed)
{
    dolog ("write_bup\n");
    if (!(s->bup_flag & BUP_SET)) {
        if (s->bup_flag & BUP_LAST) {
            int i;
            uint8_t *p = s->silence;
            for (i = 0; i < sizeof (s->silence) / 4; i++, p += 4) {
                *(uint32_t *) p = s->last_samp;
            }
        }
        else {
            memset (s->silence, 0, sizeof (s->silence));
        }
        s->bup_flag |= BUP_SET;
    }

    while (elapsed) {
        int temp = audio_MIN (elapsed, sizeof (s->silence));
        while (temp) {
            int copied = AUD_write (s->voice_po, s->silence, temp);
            if (!copied)
                return;
            temp -= copied;
            elapsed -= copied;
        }
    }
}

static int read_audio (AC97LinkState *s, AC97BusMasterRegs *r,
                       int max, int *stop)
{
    uint8_t tmpbuf[4096];
    uint32_t addr = r->bd.addr;
    uint32_t temp = r->picb << 1;
    uint32_t nread = 0;
    int to_copy = 0;
    SWVoiceIn *voice = (r - s->bm_regs) == MC_INDEX ? s->voice_mc : s->voice_pi;

    temp = audio_MIN (temp, max);

    if (!temp) {
        *stop = 1;
        return 0;
    }

    while (temp) {
        int acquired;
        to_copy = audio_MIN (temp, sizeof (tmpbuf));
        acquired = AUD_read (voice, tmpbuf, to_copy);
        if (!acquired) {
            *stop = 1;
            break;
        }
        pci_dma_write (&s->dev, addr, tmpbuf, acquired);
        temp -= acquired;
        addr += acquired;
        nread += acquired;
    }

    r->bd.addr = addr;
    return nread;
}

static void transfer_audio (AC97LinkState *s, int index, int elapsed)
{
    AC97BusMasterRegs *r = &s->bm_regs[index];
    int stop = 0;

    if (s->invalid_freq[index]) {
        AUD_log ("ac97", "attempt to use voice %d with invalid frequency %d\n",
                 index, s->invalid_freq[index]);
        return;
    }

    if (r->sr & SR_DCH) {
        if (r->cr & CR_RPBM) {
            switch (index) {
            case PO_INDEX:
                write_bup (s, elapsed);
                break;
            }
        }
        return;
    }

    while ((elapsed >> 1) && !stop) {
        int temp;

        if (!r->bd_valid) {
            dolog ("invalid bd\n");
            fetch_bd (s, r);
        }

        if (!r->picb) {
            dolog ("fresh bd %d is empty %#x %#x\n",
                   r->civ, r->bd.addr, r->bd.ctl_len);
            if (r->civ == r->lvi) {
                r->sr |= SR_DCH; /* CELV? */
                s->bup_flag = 0;
                break;
            }
            r->sr &= ~SR_CELV;
            r->civ = r->piv;
            r->piv = (r->piv + 1) % 32;
            fetch_bd (s, r);
            return;
        }

        switch (index) {
        case PO_INDEX:
            temp = write_audio (s, r, elapsed, &stop);
            elapsed -= temp;
            r->picb -= (temp >> 1);
            break;

        case PI_INDEX:
        case MC_INDEX:
            temp = read_audio (s, r, elapsed, &stop);
            elapsed -= temp;
            r->picb -= (temp >> 1);
            break;
        }

        if (!r->picb) {
            uint32_t new_sr = r->sr & ~SR_CELV;

            if (r->bd.ctl_len & BD_IOC) {
                new_sr |= SR_BCIS;
            }

            if (r->civ == r->lvi) {
                dolog ("Underrun civ (%d) == lvi (%d)\n", r->civ, r->lvi);

                new_sr |= SR_LVBCI | SR_DCH | SR_CELV;
                stop = 1;
                s->bup_flag = (r->bd.ctl_len & BD_BUP) ? BUP_LAST : 0;
            }
            else {
                r->civ = r->piv;
                r->piv = (r->piv + 1) % 32;
                fetch_bd (s, r);
            }

            update_sr (s, r, new_sr);
        }
    }
}

static void pi_callback (void *opaque, int avail)
{
    transfer_audio (opaque, PI_INDEX, avail);
}

static void mc_callback (void *opaque, int avail)
{
    transfer_audio (opaque, MC_INDEX, avail);
}

static void po_callback (void *opaque, int free)
{
    transfer_audio (opaque, PO_INDEX, free);
}

static const VMStateDescription vmstate_ac97_bm_regs = {
    .name = "ac97_bm_regs",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32 (bdbar, AC97BusMasterRegs),
        VMSTATE_UINT8 (civ, AC97BusMasterRegs),
        VMSTATE_UINT8 (lvi, AC97BusMasterRegs),
        VMSTATE_UINT16 (sr, AC97BusMasterRegs),
        VMSTATE_UINT16 (picb, AC97BusMasterRegs),
        VMSTATE_UINT8 (piv, AC97BusMasterRegs),
        VMSTATE_UINT8 (cr, AC97BusMasterRegs),
        VMSTATE_UINT32 (bd_valid, AC97BusMasterRegs),
        VMSTATE_UINT32 (bd.addr, AC97BusMasterRegs),
        VMSTATE_UINT32 (bd.ctl_len, AC97BusMasterRegs),
        VMSTATE_END_OF_LIST ()
    }
};

static int ac97_post_load (void *opaque, int version_id)
{
    uint8_t active[LAST_INDEX];
    AC97LinkState *s = opaque;

    record_select (s, mixer_load (s, AC97_Record_Select));
    set_volume (s, AC97_Master_Volume_Mute,
                mixer_load (s, AC97_Master_Volume_Mute));
    set_volume (s, AC97_PCM_Out_Volume_Mute,
                mixer_load (s, AC97_PCM_Out_Volume_Mute));
    set_volume (s, AC97_Record_Gain_Mute,
                mixer_load (s, AC97_Record_Gain_Mute));

    active[PI_INDEX] = !!(s->bm_regs[PI_INDEX].cr & CR_RPBM);
    active[PO_INDEX] = !!(s->bm_regs[PO_INDEX].cr & CR_RPBM);
    active[MC_INDEX] = !!(s->bm_regs[MC_INDEX].cr & CR_RPBM);
    reset_voices (s, active);

    s->bup_flag = 0;
    s->last_samp = 0;
    return 0;
}

static bool is_version_2 (void *opaque, int version_id)
{
    return version_id == 2;
}

static const VMStateDescription vmstate_ac97 = {
    .name = "ac97",
    .version_id = 3,
    .minimum_version_id = 2,
    .post_load = ac97_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE (dev, AC97LinkState),
        VMSTATE_UINT32 (glob_cnt, AC97LinkState),
        VMSTATE_UINT32 (glob_sta, AC97LinkState),
        VMSTATE_UINT32 (cas, AC97LinkState),
        VMSTATE_STRUCT_ARRAY (bm_regs, AC97LinkState, 3, 1,
                              vmstate_ac97_bm_regs, AC97BusMasterRegs),
        VMSTATE_BUFFER (mixer_data, AC97LinkState),
        VMSTATE_UNUSED_TEST (is_version_2, 3),
        VMSTATE_END_OF_LIST ()
    }
};

static uint64_t nam_read(void *opaque, hwaddr addr, unsigned size)
{
    if ((addr / size) > 256) {
        return -1;
    }

    switch (size) {
    case 1:
        return nam_readb(opaque, addr);
    case 2:
        return nam_readw(opaque, addr);
    case 4:
        return nam_readl(opaque, addr);
    default:
        return -1;
    }
}

static void nam_write(void *opaque, hwaddr addr, uint64_t val,
                      unsigned size)
{
    if ((addr / size) > 256) {
        return;
    }

    switch (size) {
    case 1:
        nam_writeb(opaque, addr, val);
        break;
    case 2:
        nam_writew(opaque, addr, val);
        break;
    case 4:
        nam_writel(opaque, addr, val);
        break;
    }
}

static const MemoryRegionOps ac97_io_nam_ops = {
    .read = nam_read,
    .write = nam_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t nabm_read(void *opaque, hwaddr addr, unsigned size)
{
    if ((addr / size) > 64) {
        return -1;
    }

    switch (size) {
    case 1:
        return nabm_readb(opaque, addr);
    case 2:
        return nabm_readw(opaque, addr);
    case 4:
        return nabm_readl(opaque, addr);
    default:
        return -1;
    }
}

static void nabm_write(void *opaque, hwaddr addr, uint64_t val,
                      unsigned size)
{
    if ((addr / size) > 64) {
        return;
    }

    switch (size) {
    case 1:
        nabm_writeb(opaque, addr, val);
        break;
    case 2:
        nabm_writew(opaque, addr, val);
        break;
    case 4:
        nabm_writel(opaque, addr, val);
        break;
    }
}


static const MemoryRegionOps ac97_io_nabm_ops = {
    .read = nabm_read,
    .write = nabm_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ac97_on_reset (void *opaque)
{
    AC97LinkState *s = opaque;

    reset_bm_regs (s, &s->bm_regs[0]);
    reset_bm_regs (s, &s->bm_regs[1]);
    reset_bm_regs (s, &s->bm_regs[2]);

    /*
     * Reset the mixer too. The Windows XP driver seems to rely on
     * this. At least it wants to read the vendor id before it resets
     * the codec manually.
     */
    mixer_reset (s);
}

static int ac97_initfn (PCIDevice *dev)
{
    AC97LinkState *s = DO_UPCAST (AC97LinkState, dev, dev);
    uint8_t *c = s->dev.config;

    /* TODO: no need to override */
    c[PCI_COMMAND] = 0x00;      /* pcicmd pci command rw, ro */
    c[PCI_COMMAND + 1] = 0x00;

    /* TODO: */
    c[PCI_STATUS] = PCI_STATUS_FAST_BACK;      /* pcists pci status rwc, ro */
    c[PCI_STATUS + 1] = PCI_STATUS_DEVSEL_MEDIUM >> 8;

    c[PCI_CLASS_PROG] = 0x00;      /* pi programming interface ro */

    /* TODO set when bar is registered. no need to override. */
    /* nabmar native audio mixer base address rw */
    c[PCI_BASE_ADDRESS_0] = PCI_BASE_ADDRESS_SPACE_IO;
    c[PCI_BASE_ADDRESS_0 + 1] = 0x00;
    c[PCI_BASE_ADDRESS_0 + 2] = 0x00;
    c[PCI_BASE_ADDRESS_0 + 3] = 0x00;

    /* TODO set when bar is registered. no need to override. */
      /* nabmbar native audio bus mastering base address rw */
    c[PCI_BASE_ADDRESS_0 + 4] = PCI_BASE_ADDRESS_SPACE_IO;
    c[PCI_BASE_ADDRESS_0 + 5] = 0x00;
    c[PCI_BASE_ADDRESS_0 + 6] = 0x00;
    c[PCI_BASE_ADDRESS_0 + 7] = 0x00;

    if (s->use_broken_id) {
        c[PCI_SUBSYSTEM_VENDOR_ID] = 0x86;
        c[PCI_SUBSYSTEM_VENDOR_ID + 1] = 0x80;
        c[PCI_SUBSYSTEM_ID] = 0x00;
        c[PCI_SUBSYSTEM_ID + 1] = 0x00;
    }

    c[PCI_INTERRUPT_LINE] = 0x00;      /* intr_ln interrupt line rw */
    c[PCI_INTERRUPT_PIN] = 0x01;      /* intr_pn interrupt pin ro */

    memory_region_init_io (&s->io_nam, OBJECT(s), &ac97_io_nam_ops, s,
                           "ac97-nam", 1024);
    memory_region_init_io (&s->io_nabm, OBJECT(s), &ac97_io_nabm_ops, s,
                           "ac97-nabm", 256);
    pci_register_bar (&s->dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_nam);
    pci_register_bar (&s->dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->io_nabm);
    qemu_register_reset (ac97_on_reset, s);
    AUD_register_card ("ac97", &s->card);
    ac97_on_reset (s);
    return 0;
}

static void ac97_exitfn (PCIDevice *dev)
{
    AC97LinkState *s = DO_UPCAST (AC97LinkState, dev, dev);

    memory_region_destroy (&s->io_nam);
    memory_region_destroy (&s->io_nabm);
}

static int ac97_init (PCIBus *bus)
{
    pci_create_simple (bus, -1, "AC97");
    return 0;
}

static Property ac97_properties[] = {
    DEFINE_PROP_UINT32 ("use_broken_id", AC97LinkState, use_broken_id, 0),
    DEFINE_PROP_END_OF_LIST (),
};

static void ac97_class_init (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS (klass);

    k->init = ac97_initfn;
    k->exit = ac97_exitfn;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82801AA_5;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_MULTIMEDIA_AUDIO;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = "Intel 82801AA AC97 Audio";
    dc->vmsd = &vmstate_ac97;
    dc->props = ac97_properties;
}

static const TypeInfo ac97_info = {
    .name          = "AC97",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof (AC97LinkState),
    .class_init    = ac97_class_init,
};

static void ac97_register_types (void)
{
    type_register_static (&ac97_info);
    pci_register_soundhw("ac97", "Intel 82801AA AC97 Audio", ac97_init);
}

type_init (ac97_register_types)
