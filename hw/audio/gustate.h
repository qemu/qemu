/*
 * GUSEMU32 - persistent GUS register state
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

#ifndef GUSTATE_H
#define GUSTATE_H

/*state block offset*/
#define gusdata (0)

/* data stored using this structure is in host byte order! */

/*access type*/
#define PortRead  (0)
#define PortWrite (1)

#define Port8Bitacc  (0)
#define Port16Bitacc (1)

/*voice register offsets (in bytes)*/
#define VSRegs (0)
#define VSRControl          (0)
#define VSRegsEnd (VSRControl+VSRegs + 32*(16*2))
#define VSRFreq             (2)
#define VSRLoopStartHi      (4)
#define VSRLoopStartLo      (6)
#define VSRLoopEndHi        (8)
#define VSRLoopEndLo       (10)
#define VSRVolRampRate     (12)
#define VSRVolRampStartVol (14)
#define VSRVolRampEndVol   (16)
#define VSRCurrVol         (18)
#define VSRCurrPosHi       (20)
#define VSRCurrPosLo       (22)
#define VSRPanning         (24)
#define VSRVolRampControl  (26)

/*voice register offsets (in words)*/
#define wVSRegs (0)
#define wVSRControl         (0)
#define wVSRegsEnd (wVSRControl+wVSRegs + 32*(16))
#define wVSRFreq            (1)
#define wVSRLoopStartHi     (2)
#define wVSRLoopStartLo     (3)
#define wVSRLoopEndHi       (4)
#define wVSRLoopEndLo       (5)
#define wVSRVolRampRate     (6)
#define wVSRVolRampStartVol (7)
#define wVSRVolRampEndVol   (8)
#define wVSRCurrVol         (9)
#define wVSRCurrPosHi      (10)
#define wVSRCurrPosLo      (11)
#define wVSRPanning        (12)
#define wVSRVolRampControl (13)

/*GUS register state block: 32 voices, padding filled with remaining registers*/
#define DataRegLoByte3x4  (VSRVolRampControl+2)
#define  DataRegWord3x4 (DataRegLoByte3x4)
#define DataRegHiByte3x5  (VSRVolRampControl+2       +1)
#define DMA_2xB (VSRVolRampControl+2+2)
#define IRQ_2xB (VSRVolRampControl+2+3)

#define RegCtrl_2xF       (VSRVolRampControl+2+(16*2))
#define Jumper_2xB        (VSRVolRampControl+2+(16*2)+1)
#define GUS42DMAStart     (VSRVolRampControl+2+(16*2)+2)

#define GUS43DRAMIOlo     (VSRVolRampControl+2+(16*2)*2)
#define  GUSDRAMPOS24bit (GUS43DRAMIOlo)
#define GUS44DRAMIOhi     (VSRVolRampControl+2+(16*2)*2+2)

#define voicewavetableirq (VSRVolRampControl+2+(16*2)*3) /* voice IRQ pseudoqueue: 1 bit per voice */

#define voicevolrampirq   (VSRVolRampControl+2+(16*2)*4) /* voice IRQ pseudoqueue: 1 bit per voice */

#define startvoices       (VSRVolRampControl+2+(16*2)*5) /* statistics / optimizations */

#define IRQStatReg2x6     (VSRVolRampControl+2+(16*2)*6)
#define TimerStatus2x8    (VSRVolRampControl+2+(16*2)*6+1)
#define TimerDataReg2x9   (VSRVolRampControl+2+(16*2)*6+2)
#define MixerCtrlReg2x0   (VSRVolRampControl+2+(16*2)*6+3)

#define VoiceSelReg3x2    (VSRVolRampControl+2+(16*2)*7)
#define FunkSelReg3x3     (VSRVolRampControl+2+(16*2)*7+1)
#define AdLibStatus2x8    (VSRVolRampControl+2+(16*2)*7+2)
#define StatRead_2xF      (VSRVolRampControl+2+(16*2)*7+3)

#define GUS48SampSpeed    (VSRVolRampControl+2+(16*2)*8)
#define GUS41DMACtrl      (VSRVolRampControl+2+(16*2)*8+1)
#define GUS45TimerCtrl    (VSRVolRampControl+2+(16*2)*8+2)
#define GUS46Counter1     (VSRVolRampControl+2+(16*2)*8+3)

#define GUS47Counter2     (VSRVolRampControl+2+(16*2)*9)
#define GUS49SampCtrl     (VSRVolRampControl+2+(16*2)*9+1)
#define GUS4cReset        (VSRVolRampControl+2+(16*2)*9+2)
#define NumVoices         (VSRVolRampControl+2+(16*2)*9+3)

#define TimerIRQs         (VSRVolRampControl+2+(16*2)*10)   /* delayed IRQ, statistics */
#define BusyTimerIRQs     (VSRVolRampControl+2+(16*2)*10+2) /* delayed IRQ, statistics */

#define AdLibCommand2xA   (VSRVolRampControl+2+(16*2)*11)
#define AdLibData2x9      (VSRVolRampControl+2+(16*2)*11+1)
#define SB2xCd            (VSRVolRampControl+2+(16*2)*11+2)
#define SB2xE             (VSRVolRampControl+2+(16*2)*11+3)

#define SynVoiceIRQ8f     (VSRVolRampControl+2+(16*2)*12)
#define GUS50DMAHigh      (VSRVolRampControl+2+(16*2)*12+1)

#define portaccesses (VSRegsEnd) /* statistics / suspend mode */

#define gusdataend (VSRegsEnd+4)

#endif /* GUSTATE_H */
