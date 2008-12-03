/*
 * QEMU Crystal CS4231 audio chip emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "hw.h"
#include "audiodev.h"
#include "audio/audio.h"
#include "isa.h"
#include "qemu-timer.h"

/*
  Missing features:
  ADC
  Loopback
  Timer
  ADPCM
  More...
*/

/* #define DEBUG */
/* #define DEBUG_XLAW */

static struct {
    int irq;
    int dma;
    int port;
    int aci_counter;
} conf = {9, 3, 0x534, 1};

#ifdef DEBUG
#define dolog(...) AUD_log ("cs4231a", __VA_ARGS__)
#else
#define dolog(...)
#endif

#define lwarn(...) AUD_log ("cs4231a", "warning: " __VA_ARGS__)
#define lerr(...) AUD_log ("cs4231a", "error: " __VA_ARGS__)

#define CS_REGS 16
#define CS_DREGS 32

typedef struct CSState {
    QEMUSoundCard card;
    qemu_irq *pic;
    uint32_t regs[CS_REGS];
    uint8_t dregs[CS_DREGS];
    int irq;
    int dma;
    int port;
    int shift;
    int dma_running;
    int audio_free;
    int transferred;
    int aci_counter;
    SWVoiceOut *voice;
    int16_t *tab;
} CSState;

#define IO_READ_PROTO(name)                             \
    static uint32_t name (void *opaque, uint32_t addr)

#define IO_WRITE_PROTO(name)                                            \
    static void name (void *opaque, uint32_t addr, uint32_t val)

#define GET_SADDR(addr) (addr & 3)

#define MODE2 (1 << 6)
#define MCE (1 << 6)
#define PMCE (1 << 4)
#define CMCE (1 << 5)
#define TE (1 << 6)
#define PEN (1 << 0)
#define INT (1 << 0)
#define IEN (1 << 1)
#define PPIO (1 << 6)
#define PI (1 << 4)
#define CI (1 << 5)
#define TI (1 << 6)

enum {
    Index_Address,
    Index_Data,
    Status,
    PIO_Data
};

enum {
    Left_ADC_Input_Control,
    Right_ADC_Input_Control,
    Left_AUX1_Input_Control,
    Right_AUX1_Input_Control,
    Left_AUX2_Input_Control,
    Right_AUX2_Input_Control,
    Left_DAC_Output_Control,
    Right_DAC_Output_Control,
    FS_And_Playback_Data_Format,
    Interface_Configuration,
    Pin_Control,
    Error_Status_And_Initialization,
    MODE_And_ID,
    Loopback_Control,
    Playback_Upper_Base_Count,
    Playback_Lower_Base_Count,
    Alternate_Feature_Enable_I,
    Alternate_Feature_Enable_II,
    Left_Line_Input_Control,
    Right_Line_Input_Control,
    Timer_Low_Base,
    Timer_High_Base,
    RESERVED,
    Alternate_Feature_Enable_III,
    Alternate_Feature_Status,
    Version_Chip_ID,
    Mono_Input_And_Output_Control,
    RESERVED_2,
    Capture_Data_Format,
    RESERVED_3,
    Capture_Upper_Base_Count,
    Capture_Lower_Base_Count
};

static int freqs[2][8] = {
    { 8000, 16000, 27420, 32000,    -1,    -1, 48000, 9000 },
    { 5510, 11025, 18900, 22050, 37800, 44100, 33075, 6620 }
};

/* Tables courtesy http://hazelware.luggle.com/tutorials/mulawcompression.html */
static int16_t MuLawDecompressTable[256] =
{
     -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
     -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
     -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
     -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
      -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
      -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
      -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
      -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
      -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
      -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
       -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
       -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
       -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
       -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
       -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
        -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
      32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
      23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
      15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
      11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
       7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
       5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
       3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
       2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
       1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
       1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
        876,   844,   812,   780,   748,   716,   684,   652,
        620,   588,   556,   524,   492,   460,   428,   396,
        372,   356,   340,   324,   308,   292,   276,   260,
        244,   228,   212,   196,   180,   164,   148,   132,
        120,   112,   104,    96,    88,    80,    72,    64,
         56,    48,    40,    32,    24,    16,     8,     0
};

static int16_t ALawDecompressTable[256] =
{
     -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
     -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
     -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
     -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
     -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,
     -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
     -11008,-10496,-12032,-11520,-8960, -8448, -9984, -9472,
     -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
     -344,  -328,  -376,  -360,  -280,  -264,  -312,  -296,
     -472,  -456,  -504,  -488,  -408,  -392,  -440,  -424,
     -88,   -72,   -120,  -104,  -24,   -8,    -56,   -40,
     -216,  -200,  -248,  -232,  -152,  -136,  -184,  -168,
     -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
     -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
     -688,  -656,  -752,  -720,  -560,  -528,  -624,  -592,
     -944,  -912,  -1008, -976,  -816,  -784,  -880,  -848,
      5504,  5248,  6016,  5760,  4480,  4224,  4992,  4736,
      7552,  7296,  8064,  7808,  6528,  6272,  7040,  6784,
      2752,  2624,  3008,  2880,  2240,  2112,  2496,  2368,
      3776,  3648,  4032,  3904,  3264,  3136,  3520,  3392,
      22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
      30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
      11008, 10496, 12032, 11520, 8960,  8448,  9984,  9472,
      15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
      344,   328,   376,   360,   280,   264,   312,   296,
      472,   456,   504,   488,   408,   392,   440,   424,
      88,    72,   120,   104,    24,     8,    56,    40,
      216,   200,   248,   232,   152,   136,   184,   168,
      1376,  1312,  1504,  1440,  1120,  1056,  1248,  1184,
      1888,  1824,  2016,  1952,  1632,  1568,  1760,  1696,
      688,   656,   752,   720,   560,   528,   624,   592,
      944,   912,  1008,   976,   816,   784,   880,   848
};

static void cs_reset(void *opaque)
{
    CSState *s = opaque;

    s->regs[Index_Address] = 0x40;
    s->regs[Index_Data]    = 0x00;
    s->regs[Status]        = 0x00;
    s->regs[PIO_Data]      = 0x00;

    s->dregs[Left_ADC_Input_Control]          = 0x00;
    s->dregs[Right_ADC_Input_Control]         = 0x00;
    s->dregs[Left_AUX1_Input_Control]         = 0x88;
    s->dregs[Right_AUX1_Input_Control]        = 0x88;
    s->dregs[Left_AUX2_Input_Control]         = 0x88;
    s->dregs[Right_AUX2_Input_Control]        = 0x88;
    s->dregs[Left_DAC_Output_Control]         = 0x80;
    s->dregs[Right_DAC_Output_Control]        = 0x80;
    s->dregs[FS_And_Playback_Data_Format]     = 0x00;
    s->dregs[Interface_Configuration]         = 0x08;
    s->dregs[Pin_Control]                     = 0x00;
    s->dregs[Error_Status_And_Initialization] = 0x00;
    s->dregs[MODE_And_ID]                     = 0x8a;
    s->dregs[Loopback_Control]                = 0x00;
    s->dregs[Playback_Upper_Base_Count]       = 0x00;
    s->dregs[Playback_Lower_Base_Count]       = 0x00;
    s->dregs[Alternate_Feature_Enable_I]      = 0x00;
    s->dregs[Alternate_Feature_Enable_II]     = 0x00;
    s->dregs[Left_Line_Input_Control]         = 0x88;
    s->dregs[Right_Line_Input_Control]        = 0x88;
    s->dregs[Timer_Low_Base]                  = 0x00;
    s->dregs[Timer_High_Base]                 = 0x00;
    s->dregs[RESERVED]                        = 0x00;
    s->dregs[Alternate_Feature_Enable_III]    = 0x00;
    s->dregs[Alternate_Feature_Status]        = 0x00;
    s->dregs[Version_Chip_ID]                 = 0xa0;
    s->dregs[Mono_Input_And_Output_Control]   = 0xa0;
    s->dregs[RESERVED_2]                      = 0x00;
    s->dregs[Capture_Data_Format]             = 0x00;
    s->dregs[RESERVED_3]                      = 0x00;
    s->dregs[Capture_Upper_Base_Count]        = 0x00;
    s->dregs[Capture_Lower_Base_Count]        = 0x00;
}

static void cs_audio_callback (void *opaque, int free)
{
    CSState *s = opaque;
    s->audio_free = free;
}

static void cs_reset_voices (CSState *s, uint32_t val)
{
    int xtal;
    struct audsettings as;

#ifdef DEBUG_XLAW
    if (val == 0 || val == 32)
        val = (1 << 4) | (1 << 5);
#endif

    xtal = val & 1;
    as.freq = freqs[xtal][(val >> 1) & 7];

    if (as.freq == -1) {
        lerr ("unsupported frequency (val=%#x)\n", val);
        goto error;
    }

    as.nchannels = (val & (1 << 4)) ? 2 : 1;
    as.endianness = 0;
    s->tab = NULL;

    switch ((val >> 5) & ((s->dregs[MODE_And_ID] & MODE2) ? 7 : 3)) {
    case 0:
        as.fmt = AUD_FMT_U8;
        s->shift = as.nchannels == 2;
        break;

    case 1:
        s->tab = MuLawDecompressTable;
        goto x_law;
    case 3:
        s->tab = ALawDecompressTable;
    x_law:
        as.fmt = AUD_FMT_S16;
        as.endianness = AUDIO_HOST_ENDIANNESS;
        s->shift = as.nchannels == 2;
        break;

    case 6:
        as.endianness = 1;
    case 2:
        as.fmt = AUD_FMT_S16;
        s->shift = as.nchannels;
        break;

    case 7:
    case 4:
        lerr ("attempt to use reserved format value (%#x)\n", val);
        goto error;

    case 5:
        lerr ("ADPCM 4 bit IMA compatible format is not supported\n");
        goto error;
    }

    s->voice = AUD_open_out (
        &s->card,
        s->voice,
        "cs4231a",
        s,
        cs_audio_callback,
        &as
        );

    if (s->dregs[Interface_Configuration] & PEN) {
        if (!s->dma_running) {
            DMA_hold_DREQ (s->dma);
            AUD_set_active_out (s->voice, 1);
            s->transferred = 0;
        }
        s->dma_running = 1;
    }
    else {
        if (s->dma_running) {
            DMA_release_DREQ (s->dma);
            AUD_set_active_out (s->voice, 0);
        }
        s->dma_running = 0;
    }
    return;

 error:
    if (s->dma_running) {
        DMA_release_DREQ (s->dma);
        AUD_set_active_out (s->voice, 0);
    }
}

IO_READ_PROTO (cs_read)
{
    CSState *s = opaque;
    uint32_t saddr, iaddr, ret;

    saddr = GET_SADDR (addr);
    iaddr = ~0U;

    switch (saddr) {
    case Index_Address:
        ret = s->regs[saddr] & ~0x80;
        break;

    case Index_Data:
        if (!(s->dregs[MODE_And_ID] & MODE2))
            iaddr = s->regs[Index_Address] & 0x0f;
        else
            iaddr = s->regs[Index_Address] & 0x1f;

        ret = s->dregs[iaddr];
        if (iaddr == Error_Status_And_Initialization) {
            /* keep SEAL happy */
            if (s->aci_counter) {
                ret |= 1 << 5;
                s->aci_counter -= 1;
            }
        }
        break;

    default:
        ret = s->regs[saddr];
        break;
    }
    dolog ("read %d:%d -> %d\n", saddr, iaddr, ret);
    return ret;
}

IO_WRITE_PROTO (cs_write)
{
    CSState *s = opaque;
    uint32_t saddr, iaddr;

    saddr = GET_SADDR (addr);

    switch (saddr) {
    case Index_Address:
        if (!(s->regs[Index_Address] & MCE) && (val & MCE)
            && (s->dregs[Interface_Configuration] & (3 << 3)))
            s->aci_counter = conf.aci_counter;

        s->regs[Index_Address] = val & ~(1 << 7);
        break;

    case Index_Data:
        if (!(s->dregs[MODE_And_ID] & MODE2))
            iaddr = s->regs[Index_Address] & 0x0f;
        else
            iaddr = s->regs[Index_Address] & 0x1f;

        switch (iaddr) {
        case RESERVED:
        case RESERVED_2:
        case RESERVED_3:
            lwarn ("attempt to write %#x to reserved indirect register %d\n",
                   val, iaddr);
            break;

        case FS_And_Playback_Data_Format:
            if (s->regs[Index_Address] & MCE) {
                cs_reset_voices (s, val);
            }
            else {
                if (s->dregs[Alternate_Feature_Status] & PMCE) {
                    val = (val & ~0x0f) | (s->dregs[iaddr] & 0x0f);
                    cs_reset_voices (s, val);
                }
                else {
                    lwarn ("[P]MCE(%#x, %#x) is not set, val=%#x\n",
                           s->regs[Index_Address],
                           s->dregs[Alternate_Feature_Status],
                           val);
                    break;
                }
            }
            s->dregs[iaddr] = val;
            break;

        case Interface_Configuration:
            val &= ~(1 << 5);   /* D5 is reserved */
            s->dregs[iaddr] = val;
            if (val & PPIO) {
                lwarn ("PIO is not supported (%#x)\n", val);
                break;
            }
            if (val & PEN) {
                if (!s->dma_running) {
                    cs_reset_voices (s, s->dregs[FS_And_Playback_Data_Format]);
                }
            }
            else {
                if (s->dma_running) {
                    DMA_release_DREQ (s->dma);
                    AUD_set_active_out (s->voice, 0);
                    s->dma_running = 0;
                }
            }
            break;

        case Error_Status_And_Initialization:
            lwarn ("attempt to write to read only register %d\n", iaddr);
            break;

        case MODE_And_ID:
            dolog ("val=%#x\n", val);
            if (val & MODE2)
                s->dregs[iaddr] |= MODE2;
            else
                s->dregs[iaddr] &= ~MODE2;
            break;

        case Alternate_Feature_Enable_I:
            if (val & TE)
                lerr ("timer is not yet supported\n");
            s->dregs[iaddr] = val;
            break;

        case Alternate_Feature_Status:
            if ((s->dregs[iaddr] & PI) && !(val & PI)) {
                /* XXX: TI CI */
                qemu_irq_lower (s->pic[s->irq]);
                s->regs[Status] &= ~INT;
            }
            s->dregs[iaddr] = val;
            break;

        case Version_Chip_ID:
            lwarn ("write to Version_Chip_ID register %#x\n", val);
            s->dregs[iaddr] = val;
            break;

        default:
            s->dregs[iaddr] = val;
            break;
        }
        dolog ("written value %#x to indirect register %d\n", val, iaddr);
        break;

    case Status:
        if (s->regs[Status] & INT) {
            qemu_irq_lower (s->pic[s->irq]);
        }
        s->regs[Status] &= ~INT;
        s->dregs[Alternate_Feature_Status] &= ~(PI | CI | TI);
        break;

    case PIO_Data:
        lwarn ("attempt to write value %#x to PIO register\n", val);
        break;
    }
}

static int cs_write_audio (CSState *s, int nchan, int dma_pos,
                           int dma_len, int len)
{
    int temp, net;
    uint8_t tmpbuf[4096];

    temp = len;
    net = 0;

    while (temp) {
        int left = dma_len - dma_pos;
        int copied;
        size_t to_copy;

        to_copy = audio_MIN (temp, left);
        if (to_copy > sizeof (tmpbuf)) {
            to_copy = sizeof (tmpbuf);
        }

        copied = DMA_read_memory (nchan, tmpbuf, dma_pos, to_copy);
        if (s->tab) {
            int i;
            int16_t linbuf[4096];

            for (i = 0; i < copied; ++i)
                linbuf[i] = s->tab[tmpbuf[i]];
            copied = AUD_write (s->voice, linbuf, copied << 1);
            copied >>= 1;
        }
        else {
            copied = AUD_write (s->voice, tmpbuf, copied);
        }

        temp -= copied;
        dma_pos = (dma_pos + copied) % dma_len;
        net += copied;

        if (!copied) {
            break;
        }
    }

    return net;
}

static int cs_dma_read (void *opaque, int nchan, int dma_pos, int dma_len)
{
    CSState *s = opaque;
    int copy, written;
    int till = -1;

    copy = s->voice ? (s->audio_free >> (s->tab != NULL)) : dma_len;

    if (s->dregs[Pin_Control] & IEN) {
        till = (s->dregs[Playback_Lower_Base_Count]
            | (s->dregs[Playback_Upper_Base_Count] << 8)) << s->shift;
        till -= s->transferred;
        copy = audio_MIN (till, copy);
    }

    if ((copy <= 0) || (dma_len <= 0)) {
        return dma_pos;
    }

    written = cs_write_audio (s, nchan, dma_pos, dma_len, copy);

    dma_pos = (dma_pos + written) % dma_len;
    s->audio_free -= (written << (s->tab != NULL));

    if (written == till) {
        s->regs[Status] |= INT;
        s->dregs[Alternate_Feature_Status] |= PI;
        s->transferred = 0;
        qemu_irq_raise (s->pic[s->irq]);
    }
    else {
        s->transferred += written;
    }

    return dma_pos;
}

static void cs_save(QEMUFile *f, void *opaque)
{
    CSState *s = opaque;
    unsigned int i;
    uint32_t val;

    for (i = 0; i < CS_REGS; i++)
        qemu_put_be32s(f, &s->regs[i]);

    qemu_put_buffer(f, s->dregs, CS_DREGS);
    val = s->dma_running; qemu_put_be32s(f, &val);
    val = s->audio_free;  qemu_put_be32s(f, &val);
    val = s->transferred; qemu_put_be32s(f, &val);
    val = s->aci_counter; qemu_put_be32s(f, &val);
}

static int cs_load(QEMUFile *f, void *opaque, int version_id)
{
    CSState *s = opaque;
    unsigned int i;
    uint32_t val, dma_running;

    if (version_id > 1)
        return -EINVAL;

    for (i = 0; i < CS_REGS; i++)
        qemu_get_be32s(f, &s->regs[i]);

    qemu_get_buffer(f, s->dregs, CS_DREGS);

    qemu_get_be32s(f, &dma_running);
    qemu_get_be32s(f, &val); s->audio_free  = val;
    qemu_get_be32s(f, &val); s->transferred = val;
    qemu_get_be32s(f, &val); s->aci_counter = val;
    if (dma_running && (s->dregs[Interface_Configuration] & PEN))
        cs_reset_voices (s, s->dregs[FS_And_Playback_Data_Format]);
    return 0;
}

int cs4231a_init (AudioState *audio, qemu_irq *pic)
{
    int i;
    CSState *s;

    if (!audio) {
        lerr ("No audio state\n");
        return -1;
    }

    s = qemu_mallocz (sizeof (*s));
    if (!s) {
        lerr ("Could not allocate memory for cs4231a (%zu bytes)\n",
               sizeof (*s));
        return -1;
    }

    s->pic = pic;
    s->irq = conf.irq;
    s->dma = conf.dma;
    s->port = conf.port;

    for (i = 0; i < 4; i++) {
        register_ioport_write (s->port + i, 1, 1, cs_write, s);
        register_ioport_read (s->port + i, 1, 1, cs_read, s);
    }

    DMA_register_channel (s->dma, cs_dma_read, s);

    register_savevm ("cs4231a", 0, 1, cs_save, cs_load, s);
    qemu_register_reset (cs_reset, s);
    cs_reset (s);

    AUD_register_card (audio,"cs4231a", &s->card);
    return 0;
}
