#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "panda/debug.h"
#include "panda/plugin.h"
#include "panda/common.h"
#include "exec/translator.h"
#include "panda/panda_qemu_plugin_helpers.h"


CPUState* panda_current_cpu(int index){
    CPUState *cpu = qemu_get_cpu(index);
    return cpu;
}

CPUState* panda_cpu_in_translate(void){
    return tcg_ctx->cpu;
}

TranslationBlock * panda_get_tb(void){
    const DisasContextBase *db = tcg_ctx->plugin_db;
    return db->tb;
}

static bool panda_has_callback_registered(panda_cb_type type){
    return panda_cbs[type] != NULL;
}

int panda_get_memcb_status(void){
    bool read = false;
    bool write = false;
    if (panda_has_callback_registered(PANDA_CB_PHYS_MEM_BEFORE_READ) 
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_BEFORE_READ)
    || panda_has_callback_registered(PANDA_CB_PHYS_MEM_AFTER_READ)
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_AFTER_READ))
    {
        read = true;
    }
    if (panda_has_callback_registered(PANDA_CB_PHYS_MEM_BEFORE_WRITE) 
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_BEFORE_WRITE)
    || panda_has_callback_registered(PANDA_CB_PHYS_MEM_AFTER_WRITE)
    || panda_has_callback_registered(PANDA_CB_VIRT_MEM_AFTER_WRITE))
    {
        write = true;
    }
    if (read && write){
        return QEMU_PLUGIN_MEM_RW;
    }else if (read){
        return QEMU_PLUGIN_MEM_R;
    }else if (write){
        return QEMU_PLUGIN_MEM_W;
    }else{
        return 0;
    }
}
