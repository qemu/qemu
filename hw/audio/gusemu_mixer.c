/*
 * GUSEMU32 - mixing engine (similar to Interwave GF1 compatibility)
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

#include "qemu/osdep.h"
#include "gusemu.h"
#include "gustate.h"

#define GUSregb(position)  (*            (gusptr+(position)))
#define GUSregw(position)  (*(uint16_t *) (gusptr+(position)))
#define GUSregd(position)  (*(uint16_t *)(gusptr+(position)))

#define GUSvoice(position) (*(uint16_t *)(voiceptr+(position)))

/* samples are always 16bit stereo (4 bytes each, first right then left interleaved) */
void gus_mixvoices(GUSEmuState * state, unsigned int playback_freq, unsigned int numsamples,
                   int16_t *bufferpos)
{
    /* note that byte registers are stored in the upper half of each voice register! */
    uint8_t        *gusptr;
    int             Voice;
    uint16_t       *voiceptr;

    unsigned int    count;
    for (count = 0; count < numsamples * 2; count++)
        *(bufferpos + count) = 0;       /* clear */

    gusptr = state->gusdatapos;
    voiceptr = (uint16_t *) gusptr;
    if (!(GUSregb(GUS4cReset) & 0x01))  /* reset flag active? */
        return;

    for (Voice = 0; Voice <= (GUSregb(NumVoices) & 31); Voice++)
    {
        if (GUSvoice(wVSRControl)        &  0x200)
            GUSvoice(wVSRControl)        |= 0x100; /* voice stop request */
        if (GUSvoice(wVSRVolRampControl) &  0x200)
            GUSvoice(wVSRVolRampControl) |= 0x100; /* Volume ramp stop request */
        if (!(GUSvoice(wVSRControl) & GUSvoice(wVSRVolRampControl) & 0x100)) /* neither voice nor volume calculation active - save some time here ;) */
        {
            unsigned int    sample;

            unsigned int    LoopStart = (GUSvoice(wVSRLoopStartHi) << 16) | GUSvoice(wVSRLoopStartLo); /* 23.9 format */
            unsigned int    LoopEnd   = (GUSvoice(wVSRLoopEndHi)   << 16) | GUSvoice(wVSRLoopEndLo);   /* 23.9 format */
            unsigned int    CurrPos   = (GUSvoice(wVSRCurrPosHi)   << 16) | GUSvoice(wVSRCurrPosLo);   /* 23.9 format */
            int             VoiceIncrement = ((((unsigned long) GUSvoice(wVSRFreq) * 44100) / playback_freq) * (14 >> 1)) /
                                             ((GUSregb(NumVoices) & 31) + 1); /* 6.10 increment/frame to 23.9 increment/sample */

            int             PanningPos = (GUSvoice(wVSRPanning) >> 8) & 0xf;

            unsigned int    Volume32   = 32 * GUSvoice(wVSRCurrVol); /* 32 times larger than original gus for maintaining precision while ramping */
            unsigned int    StartVol32 = (GUSvoice(wVSRVolRampStartVol) & 0xff00) * 32;
            unsigned int    EndVol32   = (GUSvoice(wVSRVolRampEndVol)   & 0xff00) * 32;
            int             VolumeIncrement32 = (32 * 16 * (GUSvoice(wVSRVolRampRate) & 0x3f00) >> 8) >> ((((GUSvoice(wVSRVolRampRate) & 0xc000) >> 8) >> 6) * 3); /* including 1/8/64/512 volume speed divisor */
            VolumeIncrement32 = (((VolumeIncrement32 * 44100 / 2) / playback_freq) * 14) / ((GUSregb(NumVoices) & 31) + 1); /* adjust ramping speed to playback speed */

            if (GUSvoice(wVSRControl) & 0x4000)
                VoiceIncrement    = -VoiceIncrement;    /* reverse playback */
            if (GUSvoice(wVSRVolRampControl) & 0x4000)
                VolumeIncrement32 = -VolumeIncrement32; /* reverse ramping */

            for (sample = 0; sample < numsamples; sample++)
            {
                int             sample1, sample2, Volume;
                if (GUSvoice(wVSRControl) & 0x400)      /* 16bit */
                {
                    int offset = ((CurrPos >> 9) & 0xc0000) + (((CurrPos >> 9) & 0x1ffff) << 1);
                    int8_t *adr;
                    adr = (int8_t *) state->himemaddr + offset;
                    sample1 = (*adr & 0xff) + (*(adr + 1) * 256);
                    sample2 = (*(adr + 2) & 0xff) + (*(adr + 2 + 1) * 256);
                }
                else            /* 8bit */
                {
                    int offset = (CurrPos >> 9) & 0xfffff;
                    int8_t *adr;
                    adr = (int8_t *) state->himemaddr + offset;
                    sample1 = (*adr) * 256;
                    sample2 = (*(adr + 1)) * 256;
                }

                Volume = ((((Volume32 >> (4 + 5)) & 0xff) + 256) << (Volume32 >> ((4 + 8) + 5))) / 512; /* semi-logarithmic volume, +5 due to additional precision */
                sample1 = (((sample1 * Volume) >> 16) * (512 - (CurrPos % 512))) / 512;
                sample2 = (((sample2 * Volume) >> 16) * (CurrPos % 512)) / 512;
                sample1 += sample2;

                if (!(GUSvoice(wVSRVolRampControl) & 0x100))
                {
                    Volume32 += VolumeIncrement32;
                    if ((GUSvoice(wVSRVolRampControl) & 0x4000) ? (Volume32 <= StartVol32) : (Volume32 >= EndVol32)) /* ramp up boundary cross */
                    {
                        if (GUSvoice(wVSRVolRampControl) & 0x2000)
                            GUSvoice(wVSRVolRampControl) |= 0x8000;     /* volramp IRQ enabled? -> IRQ wait flag */
                        if (GUSvoice(wVSRVolRampControl) & 0x800)       /* loop enabled */
                        {
                            if (GUSvoice(wVSRVolRampControl) & 0x1000)  /* bidir. loop */
                            {
                                GUSvoice(wVSRVolRampControl) ^= 0x4000; /* toggle dir */
                                VolumeIncrement32 = -VolumeIncrement32;
                            }
                            else
                                Volume32 = (GUSvoice(wVSRVolRampControl) & 0x4000) ? EndVol32 : StartVol32; /* unidir. loop ramp */
                        }
                        else
                        {
                            GUSvoice(wVSRVolRampControl) |= 0x100;
                            Volume32 =
                                (GUSvoice(wVSRVolRampControl) & 0x4000) ? StartVol32 : EndVol32;
                        }
                    }
                }
                if ((GUSvoice(wVSRVolRampControl) & 0xa000) == 0xa000)  /* volramp IRQ set and enabled? */
                {
                    GUSregd(voicevolrampirq) |= 1 << Voice;             /* set irq slot */
                }
                else
                {
                    GUSregd(voicevolrampirq) &= (~(1 << Voice));        /* clear irq slot */
                    GUSvoice(wVSRVolRampControl) &= 0x7f00;
                }

                if (!(GUSvoice(wVSRControl) & 0x100))
                {
                    CurrPos += VoiceIncrement;
                    if ((GUSvoice(wVSRControl) & 0x4000) ? (CurrPos <= LoopStart) : (CurrPos >= LoopEnd)) /* playback boundary cross */
                    {
                        if (GUSvoice(wVSRControl) & 0x2000)
                            GUSvoice(wVSRControl) |= 0x8000;       /* voice IRQ enabled -> IRQ wait flag */
                        if (GUSvoice(wVSRControl) & 0x800)         /* loop enabled */
                        {
                            if (GUSvoice(wVSRControl) & 0x1000)    /* pingpong loop */
                            {
                                GUSvoice(wVSRControl) ^= 0x4000;   /* toggle dir */
                                VoiceIncrement = -VoiceIncrement;
                            }
                            else
                                CurrPos = (GUSvoice(wVSRControl) & 0x4000) ? LoopEnd : LoopStart; /* unidir. loop */
                        }
                        else if (!(GUSvoice(wVSRVolRampControl) & 0x400))
                            GUSvoice(wVSRControl) |= 0x100;        /* loop disabled, rollover check */
                    }
                }
                if ((GUSvoice(wVSRControl) & 0xa000) == 0xa000)    /* wavetable IRQ set and enabled? */
                {
                    GUSregd(voicewavetableirq) |= 1 << Voice;      /* set irq slot */
                }
                else
                {
                    GUSregd(voicewavetableirq) &= (~(1 << Voice)); /* clear irq slot */
                    GUSvoice(wVSRControl) &= 0x7f00;
                }

                /* mix samples into buffer */
                *(bufferpos + 2 * sample)     += (int16_t) ((sample1 * PanningPos) >> 4);        /* right */
                *(bufferpos + 2 * sample + 1) += (int16_t) ((sample1 * (15 - PanningPos)) >> 4); /* left */
            }
            /* write back voice and volume */
            GUSvoice(wVSRCurrVol)   = Volume32 / 32;
            GUSvoice(wVSRCurrPosHi) = CurrPos >> 16;
            GUSvoice(wVSRCurrPosLo) = CurrPos & 0xffff;
        }
        voiceptr += 16; /* next voice */
    }
}

void gus_irqgen(GUSEmuState * state, unsigned int elapsed_time)
/* time given in microseconds */
{
    int             requestedIRQs = 0;
    uint8_t        *gusptr;
    gusptr = state->gusdatapos;
    if (GUSregb(TimerDataReg2x9) & 1) /* start timer 1 (80us decrement rate) */
    {
        unsigned int    timer1fraction = state->timer1fraction;
        int             newtimerirqs;
        newtimerirqs          = (elapsed_time + timer1fraction) / (80 * (256 - GUSregb(GUS46Counter1)));
        state->timer1fraction = (elapsed_time + timer1fraction) % (80 * (256 - GUSregb(GUS46Counter1)));
        if (newtimerirqs)
        {
            if (!(GUSregb(TimerDataReg2x9) & 0x40))
                GUSregb(TimerStatus2x8) |= 0xc0; /* maskable bits */
            if (GUSregb(GUS45TimerCtrl) & 4)     /* timer1 irq enable */
            {
                GUSregb(TimerStatus2x8) |= 4;    /* nonmaskable bit */
                GUSregb(IRQStatReg2x6)  |= 4;    /* timer 1 irq pending */
                GUSregw(TimerIRQs) += newtimerirqs;
                requestedIRQs += newtimerirqs;
            }
        }
    }
    if (GUSregb(TimerDataReg2x9) & 2) /* start timer 2 (320us decrement rate) */
    {
        unsigned int timer2fraction = state->timer2fraction;
        int             newtimerirqs;
        newtimerirqs          = (elapsed_time + timer2fraction) / (320 * (256 - GUSregb(GUS47Counter2)));
        state->timer2fraction = (elapsed_time + timer2fraction) % (320 * (256 - GUSregb(GUS47Counter2)));
        if (newtimerirqs)
        {
            if (!(GUSregb(TimerDataReg2x9) & 0x20))
                GUSregb(TimerStatus2x8) |= 0xa0; /* maskable bits */
            if (GUSregb(GUS45TimerCtrl) & 8)     /* timer2 irq enable */
            {
                GUSregb(TimerStatus2x8) |= 2;    /* nonmaskable bit */
                GUSregb(IRQStatReg2x6)  |= 8;    /* timer 2 irq pending */
                GUSregw(TimerIRQs) += newtimerirqs;
                requestedIRQs += newtimerirqs;
            }
        }
    }
    if (GUSregb(GUS4cReset) & 0x4) /* synth IRQ enable */
    {
        if (GUSregd(voicewavetableirq))
            GUSregb(IRQStatReg2x6) |= 0x20;
        if (GUSregd(voicevolrampirq))
            GUSregb(IRQStatReg2x6) |= 0x40;
    }
    if ((!requestedIRQs) && GUSregb(IRQStatReg2x6))
        requestedIRQs++;
    if (GUSregb(IRQStatReg2x6))
        GUSregw(BusyTimerIRQs) = GUS_irqrequest(state, state->gusirq, requestedIRQs);
}
