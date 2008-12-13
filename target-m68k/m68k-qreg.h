enum {
#define DEFO32(name, offset) QREG_##name,
#define DEFR(name, reg, mode) QREG_##name,
#define DEFF64(name, offset) QREG_##name,
    QREG_NULL,
#include "qregs.def"
    TARGET_NUM_QREGS = 0x100
#undef DEFO32
#undef DEFR
#undef DEFF64
};
