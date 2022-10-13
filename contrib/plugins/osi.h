#include "osi_linux/osi_types.h"

QPP_CB_PROTOTYPE(void, on_get_current_process, OsiProc**);
QPP_CB_PROTOTYPE(void, on_get_process, const OsiProcHandle*, OsiProc**);
QPP_CB_PROTOTYPE(void, on_get_current_process_handle, OsiProcHandle**);

QPP_FUN_PROTOTYPE(osi, OsiProc*, get_current_process, void);
QPP_FUN_PROTOTYPE(osi, OsiProc*, get_process, const OsiProcHandle*);
QPP_FUN_PROTOTYPE(osi, OsiProcHandle*, get_current_process_handle, void);
