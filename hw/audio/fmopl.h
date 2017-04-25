#ifndef FMOPL_H
#define FMOPL_H

#include <stdint.h>

/* --- system optimize --- */
/* select bit size of output : 8 or 16 */
#define OPL_OUTPUT_BIT 16

/* compiler dependence */
#ifndef OSD_CPU_H
#define OSD_CPU_H
typedef unsigned short	UINT16;  /* unsigned 16bit */
typedef unsigned int	UINT32;  /* unsigned 32bit */
typedef signed char		INT8;    /* signed  8bit   */
typedef signed short	INT16;   /* signed 16bit   */
typedef signed int		INT32;   /* signed 32bit   */
#endif

#if (OPL_OUTPUT_BIT==16)
typedef INT16 OPLSAMPLE;
#endif
#if (OPL_OUTPUT_BIT==8)
typedef unsigned char  OPLSAMPLE;
#endif

typedef void (*OPL_TIMERHANDLER)(int channel,double interval_Sec);
typedef void (*OPL_IRQHANDLER)(int param,int irq);
typedef void (*OPL_UPDATEHANDLER)(int param,int min_interval_us);
typedef void (*OPL_PORTHANDLER_W)(int param,unsigned char data);
typedef unsigned char (*OPL_PORTHANDLER_R)(int param);

/* !!!!! here is private section , do not access there member direct !!!!! */

#define OPL_TYPE_WAVESEL   0x01  /* waveform select    */
#define OPL_TYPE_ADPCM     0x02  /* DELTA-T ADPCM unit */
#define OPL_TYPE_KEYBOARD  0x04  /* keyboard interface */
#define OPL_TYPE_IO        0x08  /* I/O port */

/* Saving is necessary for member of the 'R' mark for suspend/resume */
/* ---------- OPL one of slot  ---------- */
typedef struct fm_opl_slot {
	INT32 TL;		/* total level     :TL << 8            */
	INT32 TLL;		/* adjusted now TL                     */
	uint8_t  KSR;		/* key scale rate  :(shift down bit)   */
	INT32 *AR;		/* attack rate     :&AR_TABLE[AR<<2]   */
	INT32 *DR;		/* decay rate      :&DR_TALBE[DR<<2]   */
	INT32 SL;		/* sustin level    :SL_TALBE[SL]       */
	INT32 *RR;		/* release rate    :&DR_TABLE[RR<<2]   */
	uint8_t ksl;		/* keyscale level  :(shift down bits)  */
	uint8_t ksr;		/* key scale rate  :kcode>>KSR         */
	UINT32 mul;		/* multiple        :ML_TABLE[ML]       */
	UINT32 Cnt;		/* frequency count :                   */
	UINT32 Incr;	/* frequency step  :                   */
	/* envelope generator state */
	uint8_t eg_typ;	/* envelope type flag                  */
	uint8_t evm;		/* envelope phase                      */
	INT32 evc;		/* envelope counter                    */
	INT32 eve;		/* envelope counter end point          */
	INT32 evs;		/* envelope counter step               */
	INT32 evsa;	/* envelope step for AR :AR[ksr]           */
	INT32 evsd;	/* envelope step for DR :DR[ksr]           */
	INT32 evsr;	/* envelope step for RR :RR[ksr]           */
	/* LFO */
	uint8_t ams;		/* ams flag                            */
	uint8_t vib;		/* vibrate flag                        */
	/* wave selector */
	INT32 **wavetable;
}OPL_SLOT;

/* ---------- OPL one of channel  ---------- */
typedef struct fm_opl_channel {
	OPL_SLOT SLOT[2];
	uint8_t CON;			/* connection type                     */
	uint8_t FB;			/* feed back       :(shift down bit)   */
	INT32 *connect1;	/* slot1 output pointer                */
	INT32 *connect2;	/* slot2 output pointer                */
	INT32 op1_out[2];	/* slot1 output for selfeedback        */
	/* phase generator state */
	UINT32  block_fnum;	/* block+fnum      :                   */
	uint8_t kcode;		/* key code        : KeyScaleCode      */
	UINT32  fc;			/* Freq. Increment base                */
	UINT32  ksl_base;	/* KeyScaleLevel Base step             */
	uint8_t keyon;		/* key on/off flag                     */
} OPL_CH;

/* OPL state */
typedef struct fm_opl_f {
	uint8_t type;			/* chip type                         */
	int clock;			/* master clock  (Hz)                */
	int rate;			/* sampling rate (Hz)                */
	double freqbase;	/* frequency base                    */
	double TimerBase;	/* Timer base time (==sampling time) */
	uint8_t address;		/* address register                  */
	uint8_t status;		/* status flag                       */
	uint8_t statusmask;	/* status mask                       */
	UINT32 mode;		/* Reg.08 : CSM , notesel,etc.       */
	/* Timer */
	int T[2];			/* timer counter                     */
	uint8_t st[2];		/* timer enable                      */
	/* FM channel slots */
	OPL_CH *P_CH;		/* pointer of CH                     */
	int	max_ch;			/* maximum channel                   */
	/* Rhythm sention */
	uint8_t rhythm;		/* Rhythm mode , key flag */
	OPL_PORTHANDLER_R porthandler_r;
	OPL_PORTHANDLER_W porthandler_w;
	int port_param;
	OPL_PORTHANDLER_R keyboardhandler_r;
	OPL_PORTHANDLER_W keyboardhandler_w;
	int keyboard_param;
	/* time tables */
	INT32 AR_TABLE[75];	/* atttack rate tables */
	INT32 DR_TABLE[75];	/* decay rate tables   */
	UINT32 FN_TABLE[1024];  /* fnumber -> increment counter */
	/* LFO */
	INT32 *ams_table;
	INT32 *vib_table;
	INT32 amsCnt;
	INT32 amsIncr;
	INT32 vibCnt;
	INT32 vibIncr;
	/* wave selector enable flag */
	uint8_t wavesel;
	/* external event callback handler */
	OPL_TIMERHANDLER  TimerHandler;		/* TIMER handler   */
	int TimerParam;						/* TIMER parameter */
	OPL_IRQHANDLER    IRQHandler;		/* IRQ handler    */
	int IRQParam;						/* IRQ parameter  */
	OPL_UPDATEHANDLER UpdateHandler;	/* stream update handler   */
	int UpdateParam;					/* stream update parameter */
} FM_OPL;

/* ---------- Generic interface section ---------- */
#define OPL_TYPE_YM3812 (OPL_TYPE_WAVESEL)

FM_OPL *OPLCreate(int type, int clock, int rate);
void OPLDestroy(FM_OPL *OPL);
void OPLSetTimerHandler(FM_OPL *OPL,OPL_TIMERHANDLER TimerHandler,int channelOffset);
void OPLSetIRQHandler(FM_OPL *OPL,OPL_IRQHANDLER IRQHandler,int param);
void OPLSetUpdateHandler(FM_OPL *OPL,OPL_UPDATEHANDLER UpdateHandler,int param);

void OPLResetChip(FM_OPL *OPL);
int OPLWrite(FM_OPL *OPL,int a,int v);
unsigned char OPLRead(FM_OPL *OPL,int a);
int OPLTimerOver(FM_OPL *OPL,int c);

void YM3812UpdateOne(FM_OPL *OPL, INT16 *buffer, int length);
#endif
