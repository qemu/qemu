#ifndef HW_TULIP_H
#define HW_TULIP_H

#include "qemu/units.h"
#include "net/net.h"

#define TYPE_TULIP "tulip"
#define TULIP(obj) OBJECT_CHECK(TULIPState, (obj), TYPE_TULIP)

#define CSR(_x) ((_x) << 3)

#define CSR0_SWR        BIT(0)
#define CSR0_BAR        BIT(1)
#define CSR0_DSL_SHIFT  2
#define CSR0_DSL_MASK   0x1f
#define CSR0_BLE        BIT(7)
#define CSR0_PBL_SHIFT  8
#define CSR0_PBL_MASK   0x3f
#define CSR0_CAC_SHIFT  14
#define CSR0_CAC_MASK   0x3
#define CSR0_DAS        0x10000
#define CSR0_TAP_SHIFT  17
#define CSR0_TAP_MASK   0x7
#define CSR0_DBO        0x100000
#define CSR1_TPD        0x01
#define CSR0_RLE        BIT(23)
#define CSR0_WIE        BIT(24)

#define CSR2_RPD        0x01

#define CSR5_TI         BIT(0)
#define CSR5_TPS        BIT(1)
#define CSR5_TU         BIT(2)
#define CSR5_TJT        BIT(3)
#define CSR5_LNP_ANC    BIT(4)
#define CSR5_UNF        BIT(5)
#define CSR5_RI         BIT(6)
#define CSR5_RU         BIT(7)
#define CSR5_RPS        BIT(8)
#define CSR5_RWT        BIT(9)
#define CSR5_ETI        BIT(10)
#define CSR5_GTE        BIT(11)
#define CSR5_LNF        BIT(12)
#define CSR5_FBE        BIT(13)
#define CSR5_ERI        BIT(14)
#define CSR5_AIS        BIT(15)
#define CSR5_NIS        BIT(16)
#define CSR5_RS_SHIFT   17
#define CSR5_RS_MASK    7
#define CSR5_TS_SHIFT   20
#define CSR5_TS_MASK    7

#define CSR5_TS_STOPPED                 0
#define CSR5_TS_RUNNING_FETCH           1
#define CSR5_TS_RUNNING_WAIT_EOT        2
#define CSR5_TS_RUNNING_READ_BUF        3
#define CSR5_TS_RUNNING_SETUP           5
#define CSR5_TS_SUSPENDED               6
#define CSR5_TS_RUNNING_CLOSE           7

#define CSR5_RS_STOPPED                 0
#define CSR5_RS_RUNNING_FETCH           1
#define CSR5_RS_RUNNING_CHECK_EOR       2
#define CSR5_RS_RUNNING_WAIT_RECEIVE    3
#define CSR5_RS_SUSPENDED               4
#define CSR5_RS_RUNNING_CLOSE           5
#define CSR5_RS_RUNNING_FLUSH           6
#define CSR5_RS_RUNNING_QUEUE           7

#define CSR5_EB_SHIFT   23
#define CSR5_EB_MASK    7

#define CSR5_GPI        BIT(26)
#define CSR5_LC         BIT(27)

#define CSR6_HP         BIT(0)
#define CSR6_SR         BIT(1)
#define CSR6_HO         BIT(2)
#define CSR6_PB         BIT(3)
#define CSR6_IF         BIT(4)
#define CSR6_SB         BIT(5)
#define CSR6_PR         BIT(6)
#define CSR6_PM         BIT(7)
#define CSR6_FKD        BIT(8)
#define CSR6_FD         BIT(9)

#define CSR6_OM_SHIFT   10
#define CSR6_OM_MASK    3
#define CSR6_OM_NORMAL          0
#define CSR6_OM_INT_LOOPBACK    1
#define CSR6_OM_EXT_LOOPBACK    2

#define CSR6_FC         BIT(12)
#define CSR6_ST         BIT(13)


#define CSR6_TR_SHIFT   14
#define CSR6_TR_MASK    3
#define CSR6_TR_72      0
#define CSR6_TR_96      1
#define CSR6_TR_128     2
#define CSR6_TR_160     3

#define CSR6_CA         BIT(17)
#define CSR6_RA         BIT(30)
#define CSR6_SC         BIT(31)

#define CSR7_TIM        BIT(0)
#define CSR7_TSM        BIT(1)
#define CSR7_TUM        BIT(2)
#define CSR7_TJM        BIT(3)
#define CSR7_LPM        BIT(4)
#define CSR7_UNM        BIT(5)
#define CSR7_RIM        BIT(6)
#define CSR7_RUM        BIT(7)
#define CSR7_RSM        BIT(8)
#define CSR7_RWM        BIT(9)
#define CSR7_TMM        BIT(11)
#define CSR7_LFM        BIT(12)
#define CSR7_SEM        BIT(13)
#define CSR7_ERM        BIT(14)
#define CSR7_AIM        BIT(15)
#define CSR7_NIM        BIT(16)

#define CSR8_MISSED_FRAME_OVL           BIT(16)
#define CSR8_MISSED_FRAME_CNT_MASK      0xffff

#define CSR9_DATA_MASK  0xff
#define CSR9_SR_CS      BIT(0)
#define CSR9_SR_SK      BIT(1)
#define CSR9_SR_DI      BIT(2)
#define CSR9_SR_DO      BIT(3)
#define CSR9_REG        BIT(10)
#define CSR9_SR         BIT(11)
#define CSR9_BR         BIT(12)
#define CSR9_WR         BIT(13)
#define CSR9_RD         BIT(14)
#define CSR9_MOD        BIT(15)
#define CSR9_MDC        BIT(16)
#define CSR9_MDO        BIT(17)
#define CSR9_MII        BIT(18)
#define CSR9_MDI        BIT(19)

#define CSR11_CON       BIT(16)
#define CSR11_TIMER_MASK 0xffff

#define CSR12_MRA       BIT(0)
#define CSR12_LS100     BIT(1)
#define CSR12_LS10      BIT(2)
#define CSR12_APS       BIT(3)
#define CSR12_ARA       BIT(8)
#define CSR12_TRA       BIT(9)
#define CSR12_NSN       BIT(10)
#define CSR12_TRF       BIT(11)
#define CSR12_ANS_SHIFT 12
#define CSR12_ANS_MASK  7
#define CSR12_LPN       BIT(15)
#define CSR12_LPC_SHIFT 16
#define CSR12_LPC_MASK  0xffff

#define CSR13_SRL       BIT(0)
#define CSR13_CAC       BIT(2)
#define CSR13_AUI       BIT(3)
#define CSR13_SDM_SHIFT 4
#define CSR13_SDM_MASK  0xfff

#define CSR14_ECEN      BIT(0)
#define CSR14_LBK       BIT(1)
#define CSR14_DREN      BIT(2)
#define CSR14_LSE       BIT(3)
#define CSR14_CPEN_SHIFT 4
#define CSR14_CPEN_MASK 3
#define CSR14_MBO       BIT(6)
#define CSR14_ANE       BIT(7)
#define CSR14_RSQ       BIT(8)
#define CSR14_CSQ       BIT(9)
#define CSR14_CLD       BIT(10)
#define CSR14_SQE       BIT(11)
#define CSR14_LTE       BIT(12)
#define CSR14_APE       BIT(13)
#define CSR14_SPP       BIT(14)
#define CSR14_TAS       BIT(15)

#define CSR15_JBD       BIT(0)
#define CSR15_HUJ       BIT(1)
#define CSR15_JCK       BIT(2)
#define CSR15_ABM       BIT(3)
#define CSR15_RWD       BIT(4)
#define CSR15_RWR       BIT(5)
#define CSR15_LE1       BIT(6)
#define CSR15_LV1       BIT(7)
#define CSR15_TSCK      BIT(8)
#define CSR15_FUSQ      BIT(9)
#define CSR15_FLF       BIT(10)
#define CSR15_LSD       BIT(11)
#define CSR15_DPST      BIT(12)
#define CSR15_FRL       BIT(13)
#define CSR15_LE2       BIT(14)
#define CSR15_LV2       BIT(15)

#define RDES0_OF         BIT(0)
#define RDES0_CE         BIT(1)
#define RDES0_DB         BIT(2)
#define RDES0_RJ         BIT(4)
#define RDES0_FT         BIT(5)
#define RDES0_CS         BIT(6)
#define RDES0_TL         BIT(7)
#define RDES0_LS         BIT(8)
#define RDES0_FS         BIT(9)
#define RDES0_MF         BIT(10)
#define RDES0_RF         BIT(11)
#define RDES0_DT_SHIFT   12
#define RDES0_DT_MASK    3
#define RDES0_DE         BIT(14)
#define RDES0_ES         BIT(15)
#define RDES0_FL_SHIFT   16
#define RDES0_FL_MASK    0x3fff
#define RDES0_FF         BIT(30)
#define RDES0_OWN        BIT(31)

#define RDES1_BUF1_SIZE_SHIFT 0
#define RDES1_BUF1_SIZE_MASK 0x7ff

#define RDES1_BUF2_SIZE_SHIFT 11
#define RDES1_BUF2_SIZE_MASK 0x7ff
#define RDES1_RCH       BIT(24)
#define RDES1_RER       BIT(25)

#define TDES0_DE        BIT(0)
#define TDES0_UF        BIT(1)
#define TDES0_LF        BIT(2)
#define TDES0_CC_SHIFT  3
#define TDES0_CC_MASK   0xf
#define TDES0_HF        BIT(7)
#define TDES0_EC        BIT(8)
#define TDES0_LC        BIT(9)
#define TDES0_NC        BIT(10)
#define TDES0_LO        BIT(11)
#define TDES0_TO        BIT(14)
#define TDES0_ES        BIT(15)
#define TDES0_OWN       BIT(31)

#define TDES1_BUF1_SIZE_SHIFT 0
#define TDES1_BUF1_SIZE_MASK 0x7ff

#define TDES1_BUF2_SIZE_SHIFT 11
#define TDES1_BUF2_SIZE_MASK 0x7ff

#define TDES1_FT0       BIT(22)
#define TDES1_DPD       BIT(23)
#define TDES1_TCH       BIT(24)
#define TDES1_TER       BIT(25)
#define TDES1_AC        BIT(26)
#define TDES1_SET       BIT(27)
#define TDES1_FT1       BIT(28)
#define TDES1_FS        BIT(29)
#define TDES1_LS        BIT(30)
#define TDES1_IC        BIT(31)

struct tulip_descriptor {
    uint32_t status;
    uint32_t control;
    uint32_t buf_addr1;
    uint32_t buf_addr2;
};

#endif
