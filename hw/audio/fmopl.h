#ifndef FMOPL_H
#define FMOPL_H


typedef void (*OPL_TIMERHANDLER)(void *param, int channel, double interval_Sec);

/* !!!!! here is private section , do not access there member direct !!!!! */

/* Saving is necessary for member of the 'R' mark for suspend/resume */
/* ---------- OPL one of slot  ---------- */
typedef struct fm_opl_slot {
	int32_t TL;		/* total level     :TL << 8            */
	int32_t TLL;		/* adjusted now TL                     */
	uint8_t  KSR;		/* key scale rate  :(shift down bit)   */
	int32_t *AR;		/* attack rate     :&AR_TABLE[AR<<2]   */
	int32_t *DR;		/* decay rate      :&DR_TALBE[DR<<2]   */
	int32_t SL;		/* sustin level    :SL_TALBE[SL]       */
	int32_t *RR;		/* release rate    :&DR_TABLE[RR<<2]   */
	uint8_t ksl;		/* keyscale level  :(shift down bits)  */
	uint8_t ksr;		/* key scale rate  :kcode>>KSR         */
	uint32_t mul;		/* multiple        :ML_TABLE[ML]       */
	uint32_t Cnt;		/* frequency count :                   */
	uint32_t Incr;	/* frequency step  :                   */
	/* envelope generator state */
	uint8_t eg_typ;	/* envelope type flag                  */
	uint8_t evm;		/* envelope phase                      */
	int32_t evc;		/* envelope counter                    */
	int32_t eve;		/* envelope counter end point          */
	int32_t evs;		/* envelope counter step               */
	int32_t evsa;	/* envelope step for AR :AR[ksr]           */
	int32_t evsd;	/* envelope step for DR :DR[ksr]           */
	int32_t evsr;	/* envelope step for RR :RR[ksr]           */
	/* LFO */
	uint8_t ams;		/* ams flag                            */
	uint8_t vib;		/* vibrate flag                        */
	/* wave selector */
	int32_t **wavetable;
}OPL_SLOT;

/* ---------- OPL one of channel  ---------- */
typedef struct fm_opl_channel {
	OPL_SLOT SLOT[2];
	uint8_t CON;			/* connection type                     */
	uint8_t FB;			/* feed back       :(shift down bit)   */
	int32_t *connect1;	/* slot1 output pointer                */
	int32_t *connect2;	/* slot2 output pointer                */
	int32_t op1_out[2];	/* slot1 output for selfeedback        */
	/* phase generator state */
	uint32_t  block_fnum;	/* block+fnum      :                   */
	uint8_t kcode;		/* key code        : KeyScaleCode      */
	uint32_t  fc;			/* Freq. Increment base                */
	uint32_t  ksl_base;	/* KeyScaleLevel Base step             */
	uint8_t keyon;		/* key on/off flag                     */
} OPL_CH;

/* OPL state */
typedef struct fm_opl_f {
	int clock;			/* master clock  (Hz)                */
	int rate;			/* sampling rate (Hz)                */
	double freqbase;	/* frequency base                    */
	double TimerBase;	/* Timer base time (==sampling time) */
	uint8_t address;		/* address register                  */
	uint8_t status;		/* status flag                       */
	uint8_t statusmask;	/* status mask                       */
	uint32_t mode;		/* Reg.08 : CSM , notesel,etc.       */
	/* Timer */
	int T[2];			/* timer counter                     */
	uint8_t st[2];		/* timer enable                      */
	/* FM channel slots */
	OPL_CH *P_CH;		/* pointer of CH                     */
	int	max_ch;			/* maximum channel                   */
	/* Rhythm sention */
	uint8_t rhythm;		/* Rhythm mode , key flag */
	/* time tables */
	int32_t AR_TABLE[76];	/* attack rate tables  */
	int32_t DR_TABLE[76];	/* decay rate tables   */
	uint32_t FN_TABLE[1024];  /* fnumber -> increment counter */
	/* LFO */
	int32_t *ams_table;
	int32_t *vib_table;
	int32_t amsCnt;
	int32_t amsIncr;
	int32_t vibCnt;
	int32_t vibIncr;
	/* wave selector enable flag */
	uint8_t wavesel;
	/* external event callback handler */
	OPL_TIMERHANDLER  TimerHandler;		/* TIMER handler   */
    void *TimerParam; /* TIMER parameter */
} FM_OPL;

/* ---------- Generic interface section ---------- */
FM_OPL *OPLCreate(int clock, int rate);
void OPLDestroy(FM_OPL *OPL);
void OPLSetTimerHandler(FM_OPL *OPL, OPL_TIMERHANDLER TimerHandler,
                        void *param);

int OPLWrite(FM_OPL *OPL,int a,int v);
unsigned char OPLRead(FM_OPL *OPL,int a);
int OPLTimerOver(FM_OPL *OPL,int c);

void YM3812UpdateOne(FM_OPL *OPL, int16_t *buffer, int length);
#endif
