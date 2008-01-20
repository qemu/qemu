/*
 * GUSEMU32 - bus interface part
 *
 * Copyright (C) 2000-2007 Tibor "TS" Schütz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * TODO: check mixer: see 7.20 of sdk for panning pos (applies to all gus models?)?
 */

#include "gustate.h"
#include "gusemu.h"

#define GUSregb(position) (*            (gusptr+(position)))
#define GUSregw(position) (*(GUSword *) (gusptr+(position)))
#define GUSregd(position) (*(GUSdword *)(gusptr+(position)))

/* size given in bytes */
unsigned int gus_read(GUSEmuState * state, int port, int size)
{
    int             value_read = 0;

    GUSbyte        *gusptr;
    gusptr = state->gusdatapos;
    GUSregd(portaccesses)++;

    switch (port & 0xff0f)
    {
        /* MixerCtrlReg (read not supported on GUS classic) */
        /* case 0x200: return GUSregb(MixerCtrlReg2x0); */
    case 0x206:                          /* IRQstatReg / SB2x6IRQ */
        /* adlib/sb bits set in port handlers */
        /* timer/voice bits set in gus_irqgen() */
        /* dma bit set in gus_dma_transferdata */
        /* midi not implemented yet */
        return GUSregb(IRQStatReg2x6);
    /* case 0x308:                       */ /* AdLib388 */
    case 0x208:
        if (GUSregb(GUS45TimerCtrl) & 1)
            return GUSregb(TimerStatus2x8);
        return GUSregb(AdLibStatus2x8);  /* AdLibStatus */
    case 0x309:                          /* AdLib389 */
    case 0x209:
        return GUSregb(AdLibData2x9);    /* AdLibData */
    case 0x20A:
        return GUSregb(AdLibCommand2xA); /* AdLib2x8_2xA */

#if 0
    case 0x20B:                          /* GUS hidden registers (read not supported on GUS classic) */
        switch (GUSregb(RegCtrl_2xF) & 0x07)
        {
        case 0:                                 /* IRQ/DMA select */
            if (GUSregb(MixerCtrlReg2x0) & 0x40)
                return GUSregb(IRQ_2xB);        /* control register select bit */
            else
                return GUSregb(DMA_2xB);
            /* case 1-5:                        */ /* general purpose emulation regs  */
            /*  return ...                      */ /* + status reset reg (write only) */
        case 6:
            return GUSregb(Jumper_2xB);         /* Joystick/MIDI enable (JumperReg) */
        default:;
        }
        break;
#endif

    case 0x20C:                          /* SB2xCd */
        value_read = GUSregb(SB2xCd);
        if (GUSregb(StatRead_2xF) & 0x20)
            GUSregb(SB2xCd) ^= 0x80; /* toggle MSB on read */
        return value_read;
        /* case 0x20D:                   */ /* SB2xD is write only -> 2xE writes to it*/
    case 0x20E:
        if (GUSregb(RegCtrl_2xF) & 0x80) /* 2xE read IRQ enabled? */
        {
            GUSregb(StatRead_2xF) |= 0x80;
            GUS_irqrequest(state, state->gusirq, 1);
        }
        return GUSregb(SB2xE);           /* SB2xE */
    case 0x20F:                          /* StatRead_2xF */
        /*set/clear fixed bits */
        /*value_read = (GUSregb(StatRead_2xF) & 0xf9)|1; */ /*(LSB not set on GUS classic!)*/
        value_read = (GUSregb(StatRead_2xF) & 0xf9);
        if (GUSregb(MixerCtrlReg2x0) & 0x08)
            value_read |= 2;    /* DMA/IRQ enabled flag */
        return value_read;
    /* case 0x300:                      */ /* MIDI (not implemented) */
    /* case 0x301:                      */ /* MIDI (not implemented) */
    case 0x302:
        return GUSregb(VoiceSelReg3x2); /* VoiceSelReg */
    case 0x303:
        return GUSregb(FunkSelReg3x3);  /* FunkSelReg */
    case 0x304:                         /* DataRegLoByte3x4 + DataRegWord3x4 */
    case 0x305:                         /* DataRegHiByte3x5 */
        switch (GUSregb(FunkSelReg3x3))
        {
    /* common functions */
        case 0x41:                      /* DramDMAContrReg */
            value_read = GUSregb(GUS41DMACtrl); /* &0xfb */
            GUSregb(GUS41DMACtrl) &= 0xbb;
            if (state->gusdma >= 4)
                value_read |= 0x04;
            if (GUSregb(IRQStatReg2x6) & 0x80)
            {
                value_read |= 0x40;
                GUSregb(IRQStatReg2x6) &= 0x7f;
                if (!GUSregb(IRQStatReg2x6))
                    GUS_irqclear(state, state->gusirq);
            }
            return (GUSbyte) value_read;
            /* DramDMAmemPosReg */
            /* case 0x42: value_read=GUSregw(GUS42DMAStart); break;*/
            /* 43h+44h write only */
        case 0x45:
            return GUSregb(GUS45TimerCtrl);         /* TimerCtrlReg */
            /* 46h+47h write only */
            /* 48h: samp freq - write only */
        case 0x49:
            return GUSregb(GUS49SampCtrl) & 0xbf;   /* SampCtrlReg */
        /* case 4bh:                                */ /* joystick trim not supported */
        /* case 0x4c: return GUSregb(GUS4cReset);   */ /* GUSreset: write only*/
    /* voice specific functions */
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83:
        case 0x84:
        case 0x85:
        case 0x86:
        case 0x87:
        case 0x88:
        case 0x89:
        case 0x8a:
        case 0x8b:
        case 0x8c:
        case 0x8d:
            {
                int             offset = 2 * (GUSregb(FunkSelReg3x3) & 0x0f);
                offset += ((int) GUSregb(VoiceSelReg3x2) & 0x1f) << 5; /* = Voice*32 + Funktion*2 */
                value_read = GUSregw(offset);
            }
            break;
    /* voice unspecific functions */
        case 0x8e:                                  /* NumVoice */
            return GUSregb(NumVoices);
        case 0x8f:                                  /* irqstatreg */
            /* (pseudo IRQ-FIFO is processed during a gus_write(0x3X3,0x8f)) */
            return GUSregb(SynVoiceIRQ8f);
        default:
            return 0xffff;
        }
        if (size == 1)
        {
            if ((port & 0xff0f) == 0x305)
                value_read = value_read >> 8;
            value_read &= 0xff;
        }
        return (GUSword) value_read;
    /* case 0x306:                                  */ /* Mixer/Version info */
        /*  return 0xff; */ /* Pre 3.6 boards, ICS mixer NOT present */
    case 0x307:                                     /* DRAMaccess */
        {
            GUSbyte        *adr;
            adr = state->himemaddr + (GUSregd(GUSDRAMPOS24bit) & 0xfffff);
            return *adr;
        }
    default:;
    }
    return 0xffff;
}

void gus_write(GUSEmuState * state, int port, int size, unsigned int data)
{
    GUSbyte        *gusptr;
    gusptr = state->gusdatapos;
    GUSregd(portaccesses)++;

    switch (port & 0xff0f)
    {
    case 0x200:                 /* MixerCtrlReg */
        GUSregb(MixerCtrlReg2x0) = (GUSbyte) data;
        break;
    case 0x206:                 /* IRQstatReg / SB2x6IRQ */
        if (GUSregb(GUS45TimerCtrl) & 0x20) /* SB IRQ enabled? -> set 2x6IRQ bit */
        {
            GUSregb(TimerStatus2x8) |= 0x08;
            GUSregb(IRQStatReg2x6) = 0x10;
            GUS_irqrequest(state, state->gusirq, 1);
        }
        break;
    case 0x308:                /* AdLib 388h */
    case 0x208:                /* AdLibCommandReg */
        GUSregb(AdLibCommand2xA) = (GUSbyte) data;
        break;
    case 0x309:                /* AdLib 389h */
    case 0x209:                /* AdLibDataReg */
        if ((GUSregb(AdLibCommand2xA) == 0x04) && (!(GUSregb(GUS45TimerCtrl) & 1))) /* GUS auto timer mode enabled? */
        {
            if (data & 0x80)
                GUSregb(TimerStatus2x8) &= 0x1f; /* AdLib IRQ reset? -> clear maskable adl. timer int regs */
            else
                GUSregb(TimerDataReg2x9) = (GUSbyte) data;
        }
        else
        {
            GUSregb(AdLibData2x9) = (GUSbyte) data;
            if (GUSregb(GUS45TimerCtrl) & 0x02)
            {
                GUSregb(TimerStatus2x8) |= 0x01;
                GUSregb(IRQStatReg2x6) = 0x10;
                GUS_irqrequest(state, state->gusirq, 1);
            }
        }
        break;
    case 0x20A:
        GUSregb(AdLibStatus2x8) = (GUSbyte) data;
        break;                 /* AdLibStatus2x8 */
    case 0x20B:                /* GUS hidden registers */
        switch (GUSregb(RegCtrl_2xF) & 0x7)
        {
        case 0:
            if (GUSregb(MixerCtrlReg2x0) & 0x40)
                GUSregb(IRQ_2xB) = (GUSbyte) data; /* control register select bit */
            else
                GUSregb(DMA_2xB) = (GUSbyte) data;
            break;
            /* case 1-4: general purpose emulation regs */
        case 5:                                    /* clear stat reg 2xF */
            GUSregb(StatRead_2xF) = 0; /* ToDo: is this identical with GUS classic? */
            if (!GUSregb(IRQStatReg2x6))
                GUS_irqclear(state, state->gusirq);
            break;
        case 6:                                    /* Jumper reg (Joystick/MIDI enable) */
            GUSregb(Jumper_2xB) = (GUSbyte) data;
            break;
        default:;
        }
        break;
    case 0x20C:                /* SB2xCd */
        if (GUSregb(GUS45TimerCtrl) & 0x20)
        {
            GUSregb(TimerStatus2x8) |= 0x10; /* SB IRQ enabled? -> set 2xCIRQ bit */
            GUSregb(IRQStatReg2x6) = 0x10;
            GUS_irqrequest(state, state->gusirq, 1);
        }
    case 0x20D:                /* SB2xCd no IRQ */
        GUSregb(SB2xCd) = (GUSbyte) data;
        break;
    case 0x20E:                /* SB2xE */
        GUSregb(SB2xE) = (GUSbyte) data;
        break;
    case 0x20F:
        GUSregb(RegCtrl_2xF) = (GUSbyte) data;
        break;                 /* CtrlReg2xF */
    case 0x302:                /* VoiceSelReg */
        GUSregb(VoiceSelReg3x2) = (GUSbyte) data;
        break;
    case 0x303:                /* FunkSelReg */
        GUSregb(FunkSelReg3x3) = (GUSbyte) data;
        if ((GUSbyte) data == 0x8f) /* set irqstatreg, get voicereg and clear IRQ */
        {
            int             voice;
            if (GUSregd(voicewavetableirq)) /* WavetableIRQ */
            {
                for (voice = 0; voice < 31; voice++)
                {
                    if (GUSregd(voicewavetableirq) & (1 << voice))
                    {
                        GUSregd(voicewavetableirq) ^= (1 << voice); /* clear IRQ bit */
                        GUSregb(voice << 5) &= 0x7f; /* clear voice reg irq bit */
                        if (!GUSregd(voicewavetableirq))
                            GUSregb(IRQStatReg2x6) &= 0xdf;
                        if (!GUSregb(IRQStatReg2x6))
                            GUS_irqclear(state, state->gusirq);
                        GUSregb(SynVoiceIRQ8f) = voice | 0x60; /* (bit==0 => IRQ wartend) */
                        return;
                    }
                }
            }
            else if (GUSregd(voicevolrampirq)) /* VolRamp IRQ */
            {
                for (voice = 0; voice < 31; voice++)
                {
                    if (GUSregd(voicevolrampirq) & (1 << voice))
                    {
                        GUSregd(voicevolrampirq) ^= (1 << voice); /* clear IRQ bit */
                        GUSregb((voice << 5) + VSRVolRampControl) &= 0x7f; /* clear voice volume reg irq bit */
                        if (!GUSregd(voicevolrampirq))
                            GUSregb(IRQStatReg2x6) &= 0xbf;
                        if (!GUSregb(IRQStatReg2x6))
                            GUS_irqclear(state, state->gusirq);
                        GUSregb(SynVoiceIRQ8f) = voice | 0x80; /* (bit==0 => IRQ wartend) */
                        return;
                    }
                }
            }
            GUSregb(SynVoiceIRQ8f) = 0xe8; /* kein IRQ wartet */
        }
        break;
    case 0x304:
    case 0x305:
        {
            GUSword         writedata = (GUSword) data;
            GUSword         readmask = 0x0000;
            if (size == 1)
            {
                readmask = 0xff00;
                writedata &= 0xff;
                if ((port & 0xff0f) == 0x305)
                {
                    writedata = (GUSword) (writedata << 8);
                    readmask = 0x00ff;
                }
            }
            switch (GUSregb(FunkSelReg3x3))
            {
                /* voice specific functions */
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x0d:
                {
                    int             offset;
                    if (!(GUSregb(GUS4cReset) & 0x01))
                        break;  /* reset flag active? */
                    offset = 2 * (GUSregb(FunkSelReg3x3) & 0x0f);
                    offset += (GUSregb(VoiceSelReg3x2) & 0x1f) << 5; /*  = Voice*32 + Funktion*2 */
                    GUSregw(offset) = (GUSword) ((GUSregw(offset) & readmask) | writedata);
                }
                break;
                /* voice unspecific functions */
            case 0x0e:         /* NumVoices */
                GUSregb(NumVoices) = (GUSbyte) data;
                break;
            /* case 0x0f:      */ /* read only */
                /* common functions */
            case 0x41:         /* DramDMAContrReg */
                GUSregb(GUS41DMACtrl) = (GUSbyte) data;
                if (data & 0x01)
                    GUS_dmarequest(state);
                break;
            case 0x42:         /* DramDMAmemPosReg */
                GUSregw(GUS42DMAStart) = (GUSregw(GUS42DMAStart) & readmask) | writedata;
                GUSregb(GUS50DMAHigh) &= 0xf; /* compatibility stuff... */
                break;
            case 0x43:         /* DRAMaddrLo */
                GUSregd(GUSDRAMPOS24bit) =
                    (GUSregd(GUSDRAMPOS24bit) & (readmask | 0xff0000)) | writedata;
                break;
            case 0x44:         /* DRAMaddrHi */
                GUSregd(GUSDRAMPOS24bit) =
                    (GUSregd(GUSDRAMPOS24bit) & 0xffff) | ((data & 0x0f) << 16);
                break;
            case 0x45:         /* TCtrlReg */
                GUSregb(GUS45TimerCtrl) = (GUSbyte) data;
                if (!(data & 0x20))
                    GUSregb(TimerStatus2x8) &= 0xe7;    /* sb IRQ dis? -> clear 2x8/2xC sb IRQ flags */
                if (!(data & 0x02))
                    GUSregb(TimerStatus2x8) &= 0xfe;    /* adlib data IRQ dis? -> clear 2x8 adlib IRQ flag */
                if (!(GUSregb(TimerStatus2x8) & 0x19))
                    GUSregb(IRQStatReg2x6) &= 0xef;     /* 0xe6; $$clear IRQ if both IRQ bits are inactive or cleared */
                /* catch up delayed timer IRQs: */
                if ((GUSregw(TimerIRQs) > 1) && (GUSregb(TimerDataReg2x9) & 3))
                {
                    if (GUSregb(TimerDataReg2x9) & 1)   /* start timer 1 (80us decrement rate) */
                    {
                        if (!(GUSregb(TimerDataReg2x9) & 0x40))
                            GUSregb(TimerStatus2x8) |= 0xc0;    /* maskable bits */
                        if (data & 4) /* timer1 irq enable */
                        {
                            GUSregb(TimerStatus2x8) |= 4;       /* nonmaskable bit */
                            GUSregb(IRQStatReg2x6) |= 4;        /* timer 1 irq pending */
                        }
                    }
                    if (GUSregb(TimerDataReg2x9) & 2)   /* start timer 2 (320us decrement rate) */
                    {
                        if (!(GUSregb(TimerDataReg2x9) & 0x20))
                            GUSregb(TimerStatus2x8) |= 0xa0;    /* maskable bits */
                        if (data & 8) /* timer2 irq enable */
                        {
                            GUSregb(TimerStatus2x8) |= 2;       /* nonmaskable bit */
                            GUSregb(IRQStatReg2x6) |= 8;        /* timer 2 irq pending */
                        }
                    }
                    GUSregw(TimerIRQs)--;
                    if (GUSregw(BusyTimerIRQs) > 1)
                        GUSregw(BusyTimerIRQs)--;
                    else
                        GUSregw(BusyTimerIRQs) =
                            GUS_irqrequest(state, state->gusirq, GUSregw(TimerIRQs));
                }
                else
                    GUSregw(TimerIRQs) = 0;

                if (!(data & 0x04))
                {
                    GUSregb(TimerStatus2x8) &= 0xfb; /* clear non-maskable timer1 bit */
                    GUSregb(IRQStatReg2x6)  &= 0xfb;
                }
                if (!(data & 0x08))
                {
                    GUSregb(TimerStatus2x8) &= 0xfd; /* clear non-maskable timer2 bit */
                    GUSregb(IRQStatReg2x6)  &= 0xf7;
                }
                if (!GUSregb(IRQStatReg2x6))
                    GUS_irqclear(state, state->gusirq);
                break;
            case 0x46:          /* Counter1 */
                GUSregb(GUS46Counter1) = (GUSbyte) data;
                break;
            case 0x47:          /* Counter2 */
                GUSregb(GUS47Counter2) = (GUSbyte) data;
                break;
            /* case 0x48:       */ /* sampling freq reg not emulated (same as interwave) */
            case 0x49:          /* SampCtrlReg */
                GUSregb(GUS49SampCtrl) = (GUSbyte) data;
                break;
            /* case 0x4b:       */ /* joystick trim not emulated */
            case 0x4c:          /* GUSreset */
                GUSregb(GUS4cReset) = (GUSbyte) data;
                if (!(GUSregb(GUS4cReset) & 1)) /* reset... */
                {
                    GUSregd(voicewavetableirq) = 0;
                    GUSregd(voicevolrampirq) = 0;
                    GUSregw(TimerIRQs) = 0;
                    GUSregw(BusyTimerIRQs) = 0;
                    GUSregb(NumVoices) = 0xcd;
                    GUSregb(IRQStatReg2x6) = 0;
                    GUSregb(TimerStatus2x8) = 0;
                    GUSregb(AdLibData2x9) = 0;
                    GUSregb(TimerDataReg2x9) = 0;
                    GUSregb(GUS41DMACtrl) = 0;
                    GUSregb(GUS45TimerCtrl) = 0;
                    GUSregb(GUS49SampCtrl) = 0;
                    GUSregb(GUS4cReset) &= 0xf9; /* clear IRQ and DAC enable bits */
                    GUS_irqclear(state, state->gusirq);
                }
                /* IRQ enable bit checked elsewhere */
                /* EnableDAC bit may be used by external callers */
                break;
            }
        }
        break;
    case 0x307:                /* DRAMaccess */
        {
            GUSbyte        *adr;
            adr = state->himemaddr + (GUSregd(GUSDRAMPOS24bit) & 0xfffff);
            *adr = (GUSbyte) data;
        }
        break;
    }
}

/* Attention when breaking up a single DMA transfer to multiple ones:
 * it may lead to multiple terminal count interrupts and broken transfers:
 *
 * 1. Whenever you transfer a piece of data, the gusemu callback is invoked
 * 2. The callback may generate a TC irq (if the register was set up to do so)
 * 3. The irq may result in the program using the GUS to reprogram the GUS
 *
 * Some programs also decide to upload by just checking if TC occurs
 * (via interrupt or a cleared GUS dma flag)
 * and then start the next transfer, without checking DMA state
 *
 * Thus: Always make sure to set the TC flag correctly!
 *
 * Note that the genuine GUS had a granularity of 16 bytes/words for low/high DMA
 * while later cards had atomic granularity provided by an additional GUS50DMAHigh register
 * GUSemu also uses this register to support byte-granular transfers for better compatibility
 * with emulators other than GUSemu32
 */

void gus_dma_transferdata(GUSEmuState * state, char *dma_addr, unsigned int count, int TC)
{
    /* this function gets called by the callback function as soon as a DMA transfer is about to start
     * dma_addr is a translated address within accessible memory, not the physical one,
     * count is (real dma count register)+1
     * note that the amount of bytes transfered is fully determined by values in the DMA registers
     * do not forget to update DMA states after transferring the entire block:
     * DREQ cleared & TC asserted after the _whole_ transfer */

    char           *srcaddr;
    char           *destaddr;
    char            msbmask = 0;
    GUSbyte        *gusptr;
    gusptr = state->gusdatapos;

    srcaddr = dma_addr; /* system memory address */
    {
        int             offset = (GUSregw(GUS42DMAStart) << 4) + (GUSregb(GUS50DMAHigh) & 0xf);
        if (state->gusdma >= 4)
            offset = (offset & 0xc0000) + (2 * (offset & 0x1fff0)); /* 16 bit address translation */
        destaddr = (char *) state->himemaddr + offset; /* wavetable RAM adress */
    }

    GUSregw(GUS42DMAStart) += (GUSword)  (count >> 4);                           /* ToDo: add 16bit GUS page limit? */
    GUSregb(GUS50DMAHigh)   = (GUSbyte) ((count + GUSregb(GUS50DMAHigh)) & 0xf); /* ToDo: add 16bit GUS page limit? */

    if (GUSregb(GUS41DMACtrl) & 0x02)   /* direction, 0 := sysram->gusram */
    {
        char           *tmpaddr = destaddr;
        destaddr = srcaddr;
        srcaddr = tmpaddr;
    }

    if ((GUSregb(GUS41DMACtrl) & 0x80) && (!(GUSregb(GUS41DMACtrl) & 0x02)))
        msbmask = (const char) 0x80;    /* invert MSB */
    for (; count > 0; count--)
    {
        if (GUSregb(GUS41DMACtrl) & 0x40)
            *(destaddr++) = *(srcaddr++);               /* 16 bit lobyte */
        else
            *(destaddr++) = (msbmask ^ (*(srcaddr++))); /* 8 bit */
        if (state->gusdma >= 4)
            *(destaddr++) = (msbmask ^ (*(srcaddr++))); /* 16 bit hibyte */
    }

    if (TC)
    {
        (GUSregb(GUS41DMACtrl)) &= 0xfe;        /* clear DMA request bit */
        if (GUSregb(GUS41DMACtrl) & 0x20)       /* DMA terminal count IRQ */
        {
            GUSregb(IRQStatReg2x6) |= 0x80;
            GUS_irqrequest(state, state->gusirq, 1);
        }
    }
}
