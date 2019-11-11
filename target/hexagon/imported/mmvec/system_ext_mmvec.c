/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#include <stdio.h>
#include <string.h>

//#include <arch/thread.h>
//#include <arch/memwrap.h>
//#include <arch/system.h>
//#include <arch/external_api.h>
#include "opcodes.h"
#include "insn.h"
#include "arch.h"
//#include "pmu.h"

//#include "vtcm.h"
#include "mmvec/macros.h"
#include "qemu.h"

#define TYPE_LOAD 'L'
#define TYPE_STORE 'S'
#define TYPE_FETCH 'F'
#define TYPE_ICINVA 'I'

static inline target_ulong mem_init_access(CPUHexagonState *env, int slot, size4u_t vaddr,
	   int width, enum mem_access_types mtype, int type_for_xlate)
{
    // Nothing to do for Linux user mode in qemu
    return vaddr;
}

#ifdef FIXME
// Get Arch option through thread
#define ARCH_OPT_TH(OPTION) (thread->processor_ptr->arch_proc_options->OPTION)

typedef enum {HIDE_VTCM_WARNING, SHOW_VTCM_WARNING} vtcm_warning_t;


static inline int vmemu_vtcm_page_cross(thread_t *thread, int in_tcm_new)
{
    if (thread->last_pkt->double_access && thread->last_pkt->pkt_has_vmemu_access && thread->last_pkt->pkt_access_count==1)
    {
        if((thread->last_pkt->pkt_has_vtcm_access ^ in_tcm_new))
        {
            warn("VMEMU Crossed VTCM boundry (In VTCM First Access: %d Second Access: %d)", thread->last_pkt->pkt_has_vtcm_access, in_tcm_new);
        return 1;
        }
    }
    return 0;
}

static inline int in_vtcm_space(thread_t *thread, paddr_t paddr, vtcm_warning_t vtcm_warning)
{

    if(ARCH_OPT_TH(vtcm_size)) {
        paddr_t vtcm_lowaddr = thread->processor_ptr->options->l2tcm_base + ARCH_OPT_TH(vtcm_offset);
        paddr_t vtcm_highaddr = vtcm_lowaddr+ARCH_OPT_TH(vtcm_size)-1;
        int in_tcm = ((paddr >= vtcm_lowaddr) && (paddr <= vtcm_highaddr));
        if (!in_tcm && (vtcm_warning==SHOW_VTCM_WARNING)) warn("Scatter/Gather not in TCM space causes error (%016llx <= %016llx < %016llx)",vtcm_lowaddr,paddr,vtcm_highaddr);
        return in_tcm;
    }
    return 1;
}

static inline int in_l2tcm_space(thread_t *thread, paddr_t paddr)
{
    paddr_t tcm_lowaddr =  thread->processor_ptr->options->l2tcm_base;
    paddr_t tcm_highaddr = tcm_lowaddr + arch_get_tcm_size(thread->processor_ptr) - 1;
    int in_tcm = ((paddr >= tcm_lowaddr) && (paddr <= tcm_highaddr));
    return in_tcm;
}

static inline int in_valid_coproc_memory_space(thread_t *thread, paddr_t paddr)
{
    int in_vtcm = in_vtcm_space(thread, paddr, HIDE_VTCM_WARNING);
    int in_tcm = in_l2tcm_space(thread, paddr);

    if (in_tcm) {
#ifdef VERIFICATION
        warn("Vector Load in L2TCM space, higher priority over AHB (%016llx <= %016llx < %016llx)", thread->processor_ptr->options->l2tcm_base,paddr, thread->processor_ptr->options->l2tcm_base+ARCH_OPT_TH(l2ecomem_size)-1);
#endif
        return 1;
    }


    if (in_vtcm) {
#ifdef VERIFICATION
        paddr_t vtcm_lowaddr = thread->processor_ptr->options->l2tcm_base + ARCH_OPT_TH(vtcm_offset);
        paddr_t vtcm_highaddr = vtcm_lowaddr+ARCH_OPT_TH(vtcm_size);
        warn("Vector Load in VTCM space, higher priority over AHB (%016llx <= %016llx < %016llx)",vtcm_lowaddr,paddr,vtcm_highaddr);
#endif
        return 1;
    }


    // Check AHB
    paddr_t ahb_lowaddr = thread->processor_ptr->arch_proc_options->ahb_lowaddr;
    paddr_t ahb_highaddr = thread->processor_ptr->arch_proc_options->ahb_highaddr;
#ifdef VERIFICATION	
    if (ahb_lowaddr != 0) {
#else
	if ((ahb_lowaddr != 0) && (ARCH_OPT_TH(ahb_enable))) {
#endif	
        int in_ahb = ((paddr < ahb_highaddr) && (paddr >= ahb_lowaddr));
        if (in_ahb) {
            warn("Vector LD/ST in AHB space causes error (%016llx <= %016llx < %016llx) ",ahb_lowaddr,paddr,ahb_highaddr);
            return 0;
        }
    }

    // Check AXI2
#if 0
    paddr_t axi2_lowaddr = ARCH_OPT_TH(axi2_lowaddr);
    paddr_t axi2_highaddr = ARCH_OPT_TH(axi2_highaddr);
    if (axi2_lowaddr != 0) {
       int in_axi2 = ((paddr < axi2_highaddr) && (paddr >= axi2_lowaddr));
       if (in_axi2) {
            warn("Vector LD/ST in AXI2 space causes error (%016llx <= %016llx < %016llx)",axi2_lowaddr,paddr,axi2_highaddr);
            return 0;
       }
    }
#endif
    return 1;
}




int in_vtcm_space_proc(processor_t * proc, paddr_t paddr)
{

    if(ARCHOPT(vtcm_size)) {
        paddr_t vtcm_lowaddr = proc->options->l2tcm_base + ARCHOPT(vtcm_offset);
        paddr_t vtcm_highaddr = vtcm_lowaddr+ARCHOPT(vtcm_size);
        int in_tcm = ((paddr >= vtcm_lowaddr) && (paddr < vtcm_highaddr));
        //if (!in_tcm) warn("Scatter/Gather not in TCM space causes error (%016llx <= %016llx < %016llx)",vtcm_lowaddr,paddr,vtcm_highaddr);
        return in_tcm;
    }
    return 0;
}
#endif

static inline int check_gather_store(CPUHexagonState *env) {

    // First check to see if temp vreg has been updated
    int check  = env->gather_issued;
    check &= env->is_gather_store_insn;

    // In case we don't have store, suppress gather
    if (!check) {
        env->gather_issued = 0;
        env->vtcm_pending = 0;   // Suppress any gather writes to memory
    }
    return check;
}


#ifdef FIXME
// If any byte on a vmem load is poisoned, set the register as poisoned
static inline void read_vtcm_poison(thread_t *thread, paddr_t paddr, int size)
{
#ifdef FIXME
    if (ARCH_OPT_TH(vtcm_poison_enable))
    {
        // Check if the VTCM address is a load acquire, clear poison from bytes if it is
        check_load_acquire(thread, paddr);

        // Load could be bringing in poisoned bytes
        paddr_t final_paddr = (paddr_t) paddr + (paddr_t) size;
        vtcm_state_t *vtcm_state = THREAD2STRUCT->vtcm_state_ptr;

        for(; paddr < final_paddr; paddr++)
        {
            // If any byte is poisoned, register gets poisoned
          	bytes_list_t *entry = find_byte(vtcm_state, paddr, POISON);
          	if (entry != NULL && entry->rw >= 2) {
                update_scaga_callback_info(thread->processor_ptr, &(vtcm_state->scaga_info), thread->threadId, thread->last_pkt_pcva, paddr, SG_WARN_RAW_POISON);
                warn("Reading a Write-poisoned byte. paddr = %016llx", paddr);
            }
        }
    }
#endif
}

// If any byte on a vmem load is poisoned, print the warning
static inline void read_vtcm_poison_scatter_gather_byte(thread_t *thread, paddr_t paddr)
{
#ifdef FIXME
    if (ARCH_OPT_TH(vtcm_poison_enable))
    {
      	if(find_byte(THREAD2STRUCT->vtcm_state_ptr, paddr, POISON) != NULL) {
            warn("Reading VCTM that was marked poisoned paddr = %016llx",paddr);
            update_scaga_callback_info(thread->processor_ptr, &(THREAD2STRUCT->vtcm_state_ptr->scaga_info), thread->threadId, thread->last_pkt_pcva, paddr, SG_WARN_RAW_POISON);
            warn("Reading a Write-poisoned byte. paddr = %016llx", paddr);
        }
#endif
    }
}

void gather_activeop(thread_t *thread, paddr_t paddr, int size) {
    paddr_t paddr_byte = paddr;
    paddr_t final_paddr = (paddr_t) paddr + size;

    for(; paddr_byte < final_paddr; paddr_byte++) {
      	enlist_byte(thread, paddr_byte, POISON, 2); //rw=2, write-poison
    }
  	enlist_byte(thread, paddr, SYNCED, 0);
}

static inline void write_vtcm_poison(thread_t *thread, paddr_t paddr, int size, int is_gather_store)
{
#ifdef FIXME
    if (ARCH_OPT_TH(vtcm_poison_enable))
    {
      	paddr_t final_paddr = (paddr_t) paddr +size;
        vtcm_state_t *vtcm_state = THREAD2STRUCT->vtcm_state_ptr;

        if (is_gather_store) {
            // On a gather store, add the pa as the store release address
            // locations will be marked as un-poisonable by a load acquire
        	gather_activeop(thread, paddr, size);
        } else { // check if writing to read-poisoned bytes
          	for(; paddr < final_paddr; paddr++) {
              	bytes_list_t *entry = find_byte(vtcm_state, paddr, POISON);
                if (entry != NULL) {
                  	if (entry->rw == 2) {
                      	update_scaga_callback_info(thread->processor_ptr, &(vtcm_state->scaga_info), thread->threadId, thread->last_pkt_pcva, paddr, SG_WARN_WAW_POISON);
                        warn("Writing to a write-poisoned byte. paddr = %016llx", paddr);
                    } else if (entry->rw > 0) {
                      	update_scaga_callback_info(thread->processor_ptr, &(vtcm_state->scaga_info), thread->threadId, thread->last_pkt_pcva, paddr, SG_WARN_WAR_POISON);
                        warn("Writing to a read-poisoned byte. paddr = %016llx", paddr);
                    }
                }
            }
        }
    }
#endif
}
#endif


void mem_store_vector_oddva(CPUHexagonState *env, vaddr_t vaddr, vaddr_t lookup_vaddr, int slot, int size, size1u_t* data, size1u_t* mask, unsigned invert, int use_full_va)
{
    int i;

    if (!use_full_va) {
        lookup_vaddr = vaddr; 		
    }
	
    if (!size) return;

    int is_gather_store = check_gather_store(env); /* Right Now only gather stores temp */
    if (is_gather_store) {
        memcpy(data, &env->tmp_VRegs[0].ub[0], size);
        env->VRegs_updated_tmp = 0;
        env->gather_issued = 0;
    }

    // If it's a gather store update store data from temporary register
    // And clear flag



    env->vstore_pending[slot] = 1;
    env->vstore[slot].va   = vaddr;
    env->vstore[slot].size = size;
    memcpy(&env->vstore[slot].data.ub[0], data, size);
    if (!mask) {
        memset(&env->vstore[slot].mask.ub[0], invert ? 0 : -1, size);
    } else if (invert) {
        for (i=0; i<size; i++) {
            env->vstore[slot].mask.ub[i] = !mask[i];
        }
    } else {
        memcpy(&env->vstore[slot].mask.ub[0], mask, size);
    }
    // On a gather store, overwrite the store mask to emulate dropped gathers
    if (is_gather_store) {
        memcpy(&env->vstore[slot].mask.ub[0], &env->vtcm_log.mask.ub[0], size);
    }
    for (i = 0; i < size; i++) {
        env->mem_access[slot].cdata[i] = data[i];
    }
}

void mem_load_vector_oddva(CPUHexagonState *env, vaddr_t vaddr, vaddr_t lookup_vaddr, int slot, int size, size1u_t* data, int use_full_va)
{
    int i;

    if (!use_full_va) {
        lookup_vaddr = vaddr; 		
    }

    if (!size) return;

    for (i = 0; i < size; i++) {
        get_user_u8(data[i], vaddr);
        vaddr++;
    }
}

#ifdef FIXME
void mem_fetch_vector(thread_t* thread, insn_t * insn, vaddr_t vaddr, int slot, int size)
{
	enum ext_mem_access_types access_type = access_type_vfetch;
    mem_access_info_t *maptr;
    paddr_t   paddr;
#ifdef VERIFICATION
    int slot_tmp;
#endif

	if (!ARCH_OPT_TH(mmvec_vfetch_enabled))
		return;

    FATAL_REPLAY;
    if (!size) return;

    mem_init_access(thread, slot, vaddr, size, access_type, TYPE_LOAD);
    if (EXCEPTION_DETECTED) return;
    
    maptr = &thread->mem_access[slot];
    paddr = maptr->paddr;
	int in_tcm = in_vtcm_space(thread,paddr, HIDE_VTCM_WARNING);
	if (maptr->xlate_info.memtype.device && !in_tcm) register_coproc_ldst_exception(thread,slot,vaddr);
    if (!in_valid_coproc_memory_space(thread,paddr)) register_coproc_ldst_exception(thread,slot,vaddr);
    if (EXCEPTION_DETECTED) return;

    thread->last_pkt->pkt_access_count++;
	  if(!thread->bq_on) {
#ifdef VERIFICATION
    slot_tmp = thread->ver_cur_slot;
    thread->ver_cur_slot = slot;
#endif
    MEMTRACE_LD(thread, thread->Regs[REG_PC], vaddr, paddr, size, DREAD, 0xfeedfacedeadbeefULL);
#ifdef VERIFICATION
    thread->ver_cur_slot = slot_tmp;
#endif
	  }
 

}

void mem_store_vector_oddva(thread_t* thread, insn_t * insn, vaddr_t vaddr, vaddr_t lookup_vaddr, int slot, int size, size1u_t* data, size1u_t* mask, unsigned invert, int use_full_va)
{
    paddr_t paddr;
    enum ext_mem_access_types access_type;
    mem_access_info_t *maptr;
    int i;
    mmvecx_t *mmvecx = THREAD2STRUCT;

	if (!use_full_va) {
		lookup_vaddr = vaddr; 		
	}
	
#ifdef VERIFICATION
    int slot_tmp;
	int xa = 0;
	fCUREXT_WRAP(xa);
	warn("vector store from thread: %d context: %d of %d wrapped: %d data in=%x%x%x%x", thread->threadId, GET_SSR_FIELD(SSR_XA), thread->processor_ptr->arch_proc_options->ext_contexts, xa, data[0], data[1],data[2], data[3]);
#endif
    if (!size) return;
    FATAL_REPLAY;
    if (GET_ATTRIB(insn->opcode,A_NT_VMEM)) access_type=access_type_vstore_nt;
    else access_type=access_type_vstore;





    mem_init_access_unaligned(thread, slot, lookup_vaddr, vaddr, size, access_type, TYPE_STORE);
    if (EXCEPTION_DETECTED) return;
    maptr = &thread->mem_access[slot];
    paddr = maptr->paddr;

    int in_tcm = in_vtcm_space(thread,paddr, HIDE_VTCM_WARNING);
    int is_gather_store = check_gather_store(thread, insn); /* Right Now only gather stores temp */
    //printf("mem_store_vector_oddva thread->last_pkt->double_access=%d in_tcm=%d paddr=%llx\n", thread->last_pkt->double_access, in_tcm, paddr);
    if (is_gather_store) {
        memcpy(data, &mmvecx->tmp_VRegs[0].ub[0], size);
        mmvecx->VRegs_updated_tmp = 0;
        mmvecx->gather_issued = 0;
    }
    if (vmemu_vtcm_page_cross(thread, in_tcm)) register_coproc_ldst_exception(thread,slot,lookup_vaddr);
    if (maptr->xlate_info.memtype.device && !in_tcm) register_coproc_ldst_exception(thread,slot,lookup_vaddr);
    if (!in_valid_coproc_memory_space(thread,paddr)) register_coproc_ldst_exception(thread,slot,lookup_vaddr);

    if (EXCEPTION_DETECTED) return;

    thread->last_pkt->pkt_has_vtcm_access = in_tcm;
    thread->last_pkt->pkt_access_count++;
    if (in_tcm) {
      write_vtcm_poison(thread, paddr, size, is_gather_store);
    }
    // If it's a gather store update store data from temporary register
    // And clear flag



    mmvecx->vstore_pending[slot] = 1;
    mmvecx->vstore[slot].pa   = paddr;
    mmvecx->vstore[slot].size = size;
    memcpy(&mmvecx->vstore[slot].data.ub[0], data, size);
    if (!mask) {
        memset(&mmvecx->vstore[slot].mask.ub[0], invert ? 0 : -1, size);
    } else if (invert) {
        for (i=0; i<size; i++) {
            mmvecx->vstore[slot].mask.ub[i] = !mask[i];
        }
    } else {
        memcpy(&mmvecx->vstore[slot].mask.ub[0], mask, size);
    }
    // On a gather store, overwrite the store mask to emulate dropped gathers
    if (is_gather_store) {
        memcpy(&mmvecx->vstore[slot].mask.ub[0], &mmvecx->vtcm_log.mask.ub[0], size);

        // Store all zeros
        if (!in_tcm)
        {
             memset(&mmvecx->vstore[slot].mask.ub[0], 1, size);
             memset(&mmvecx->vstore[slot].data.ub[0], 0, size);
        }
    }
    for (i=0; i<size; i++) {
        thread->mem_access[slot].cdata[i] = data[i];
    }
#ifdef VERIFICATION
    slot_tmp = thread->ver_cur_slot;
    thread->ver_cur_slot = slot;
#endif	
	if(!thread->bq_on) {
		size4u_t etm_vaddr = vaddr;
		if (use_full_va) {
			etm_vaddr = lookup_vaddr; // Z Loads use full VA for ETM report
		}
		memwrap_memtrace_st(thread, thread->Regs[REG_PC], etm_vaddr, paddr, size);
	}
#ifdef VERIFICATION
    thread->ver_cur_slot = slot_tmp;
#endif

    fVDOCHKPAGECROSS(vaddr, vaddr+size);

    return;
}
// This is redunand and should be refactored with the above store
void mem_store_release(thread_t* thread, insn_t * insn, int size, vaddr_t vaddr, vaddr_t lookup_vaddr, int type, int use_full_va)
{

    int slot = insn->slot;
    enum ext_mem_access_types access_type=access_type_vscatter_release;
    mmvecx_t *mmvecx = THREAD2STRUCT;

	if (!use_full_va) {
		lookup_vaddr = vaddr; 		
	}

    mem_init_access_unaligned(thread, slot, lookup_vaddr, vaddr, size, access_type, TYPE_STORE);
    mem_access_info_t * maptr = &thread->mem_access[slot];
    if (EXCEPTION_DETECTED) return;

    paddr_t paddr = maptr->paddr;

    // Check TCM space for Exception
	int in_tcm = in_vtcm_space(thread,paddr, HIDE_VTCM_WARNING);
    if (maptr->xlate_info.memtype.device && !in_tcm)register_coproc_ldst_exception(thread,slot,lookup_vaddr);
    if (!in_valid_coproc_memory_space(thread,paddr)) register_coproc_ldst_exception(thread,slot,lookup_vaddr);
    if (EXCEPTION_DETECTED) return;

    int in_vtcm = in_vtcm_space(thread,paddr, SHOW_VTCM_WARNING);


    // Mark Pending, but suppress with mask so it gets logged to BE
    mmvecx->vstore_pending[slot] = 1;
    mmvecx->vstore[slot].pa   = paddr;
    mmvecx->vstore[slot].size = size;
    memset(&mmvecx->vstore[slot].data.ub[0], 0, size);
    if (in_vtcm && !thread->bq_on) {
        memset(&mmvecx->vstore[slot].mask.ub[0], 0, size);
#ifdef FIXME
        // Mark previously poisoned bytes as un-poisonable
        if (ARCH_OPT_TH(vtcm_poison_enable))
        {
          //            add_store_release(mmvecx->vtcm_state_ptr, paddr);
          enlist_byte(thread, paddr, SYNCED, 0);
        }
#endif
    }
    else
        memset(&mmvecx->vstore[slot].mask.ub[0], 0xFF, size);



#ifdef VERIFICATION
    int slot_tmp = thread->ver_cur_slot;
    thread->ver_cur_slot = slot;

	if (in_vtcm && thread->processor_ptr->options->testgen_mode)
		size = 0;
	
#endif	
	if(!thread->bq_on) {
		size4u_t etm_vaddr = vaddr;
		if (use_full_va) {
			etm_vaddr = lookup_vaddr; // Z Loads use full VA for ETM report
		}
		memwrap_memtrace_st(thread, thread->Regs[REG_PC], lookup_vaddr, paddr, size);
	}
#ifdef VERIFICATION
    thread->ver_cur_slot = slot_tmp;
#endif
    return;
}


static int check_scatter_gather_page_cross(thread_t* thread, vaddr_t base, int length, int page_size)
{

#ifdef VERIFICATION
    warn("Scatter/Gather Op page cross check enable=%d base=0x%x last byte=0x%x page_size=%x", ARCH_OPT_TH(vtcm_page_cross_check), base, length, page_size);
#endif

    if(ARCH_OPT_TH(vtcm_page_cross_check))
    {
        vaddr_t page_mask = (1ULL<<page_size)-1;
        if (((base+length) & ~page_mask) != (base & ~page_mask))
        {

            warn("Scatter/Gather Op crossed page: start =%u end=%u Page Size=%d Page Mask=%x", base, base+length, page_size,page_mask);
            return 1;
        }
    }
    return 0;
}
#endif


void mem_vector_scatter_init(CPUHexagonState *env, int slot, vaddr_t base_vaddr, int length, int element_size)
{
    enum ext_mem_access_types access_type=access_type_vscatter_store;
    // Translation for Store Address on Slot 1 - maybe any slot?
    mem_init_access(env, slot, base_vaddr, 1, access_type, TYPE_STORE);
    mem_access_info_t * maptr = &env->mem_access[slot];
    if (EXCEPTION_DETECTED) return;

#ifdef FIXME
    // FIXME - TCM not modelled in qemu Linux user space
    // Check TCM space for Exception
    paddr_t base_paddr = env->mem_access[slot].paddr;

    env->mem_access[slot].paddr = env->mem_access[slot].paddr & ~(element_size-1);   // Align to element Size

	int in_tcm = in_vtcm_space(env,base_paddr,SHOW_VTCM_WARNING);
    if (maptr->xlate_info.memtype.device && !in_tcm) register_coproc_ldst_exception(env,slot,base_vaddr);

    int scatter_gather_exception =  (length < 0);
    scatter_gather_exception |= !in_tcm;
    scatter_gather_exception |= !in_vtcm_space(env,base_paddr+length, SHOW_VTCM_WARNING);
    scatter_gather_exception |= check_scatter_gather_page_cross(env,base_vaddr,length, env->mem_access[slot].xlate_info.size);
    if (scatter_gather_exception)
        register_coproc_ldst_exception(env,slot,base_vaddr);

    if (EXCEPTION_DETECTED) return;
#endif

	maptr->range = length;

    int i = 0;
    for(i = 0; i < fVECSIZE(); i++) {
        env->vtcm_log.offsets.ub[i] = 0; // Mark invalid
        env->vtcm_log.data.ub[i] = 0;
        env->vtcm_log.mask.ub[i] = 0;
    }
    env->vtcm_log.va_base = base_vaddr;

    env->vtcm_pending = 1;
    env->vtcm_log.oob_access = 0;
    env->vtcm_log.op = 0;
    env->vtcm_log.op_size = 0;
    return;
}


void mem_vector_gather_init(CPUHexagonState *env, int slot, vaddr_t base_vaddr,  int length, int element_size)
{
    enum ext_mem_access_types access_type = access_type_vgather_load;
    mem_init_access(env, slot, base_vaddr, 1,  access_type, TYPE_LOAD);
    mem_access_info_t * maptr = &env->mem_access[slot];

    if (EXCEPTION_DETECTED) return;
#ifdef FIXME
    // FIXME - TCM not modelled in qemu Linux user space
    // Check TCM space for Exception
    paddr_t base_paddr = env->mem_access[slot].paddr;



    env->mem_access[slot].paddr = env->mem_access[slot].paddr & ~(element_size-1);   // Align to element Size

    // Need to Test 4 conditions for exception
    // M register is positive
    // Base and Base+Length-1 are in TCM
    // Base + Length doesn't cross a page
	int in_tcm = in_vtcm_space(env,base_paddr, SHOW_VTCM_WARNING);
    if (maptr->xlate_info.memtype.device && !in_tcm) register_coproc_ldst_exception(env,slot,base_vaddr);

    int scatter_gather_exception =  (length < 0);
    scatter_gather_exception |= !in_tcm;
    scatter_gather_exception |= !in_vtcm_space(env,base_paddr+length, SHOW_VTCM_WARNING);
    scatter_gather_exception |= check_scatter_gather_page_cross(env,base_vaddr,length, env->mem_access[slot].xlate_info.size);
    if (scatter_gather_exception)
        register_coproc_ldst_exception(env,slot,base_vaddr);

    if (EXCEPTION_DETECTED) return;
#endif

	maptr->range = length;

    int i = 0;
    for(i = 0; i < 2*fVECSIZE(); i++) {
        env->vtcm_log.offsets.ub[i] = 0x0;
    }
    for(i = 0; i < fVECSIZE(); i++) {
        env->vtcm_log.data.ub[i] = 0;
        env->vtcm_log.mask.ub[i] = 0;
        env->vtcm_log.va[i] = 0;
        env->tmp_VRegs[0].ub[i] = 0;
    }
    env->vtcm_log.oob_access = 0;
    env->vtcm_log.op = 0;
    env->vtcm_log.op_size = 0;

    env->vtcm_log.va_base = base_vaddr;

    // Temp Reg gets updated
    // This allows Store .new to grab the correct result
    env->VRegs_updated_tmp = 1;
    env->gather_issued = 1;

#ifdef FIXME
    if ( ARCH_OPT_TH(mmvec_network_addr_log2))
        fprintf(ARCH_OPT_TH(mmvec_network_addr_log2), "3 %u %d\n", base_vaddr, length);
#endif

    return;
}



void mem_vector_scatter_finish(CPUHexagonState *env, int slot, int op)
{
    int i = 0;
    for (i = 0; i < fVECSIZE(); i++)
    {
        if ( env->vtcm_log.mask.ub[i] )
        {
#ifdef FIXME
            if (ARCH_OPT_TH(vtcm_poison_enable) && !env->bq_on)
            {
              	enlist_byte(env, env->vtcm_log.pa[i], POISON, 2); //rw=2
            }
#endif
        }
    }
    env->store_pending[slot] = 0;
    env->vstore_pending[slot] = 0;
    env->vtcm_log.size = fVECSIZE();


    memcpy(env->mem_access[slot].cdata, &env->vtcm_log.offsets.ub[0], 256);


#ifdef VERIFICATION
    int slot_tmp = env->ver_cur_slot;
    env->ver_cur_slot = slot;
#endif
    // Send bytes for scatters

#ifdef FIXME
	if(!env->bq_on) { 
		MEMTRACE_ST(env,env->Regs[REG_PC],env->vtcm_log.va_base, env->vtcm_log.pa_base,1,VSCATTER, 0x0);
		if (env->processor_ptr->options->testgen_mode)
		{
			MEMTRACE_ST(env,env->Regs[REG_PC],env->vtcm_log.va_base, env->vtcm_log.pa_base,1,VSCATTER, 0x0);
		} else{
			MEMTRACE_ST(env,env->Regs[REG_PC],env->vtcm_log.va_base, env->vtcm_log.pa_base,0,DWRITE, 0x0);
		}
 	}   
#endif

#ifdef VERIFICATION
    for (i = 0; i < fVECSIZE(); i++)
    {
        int size = env->vtcm_log.mask.ub[i] ? 1 : 0;
        paddr_t paddr_byte = env->vtcm_log.pa[i];
        vaddr_t vaddr_byte = (paddr_byte - env->vtcm_log.pa_base) + env->vtcm_log.va_base;
		if (env->processor_ptr->options->testgen_mode)
		{
			MEMTRACE_ST(env,env->Regs[REG_PC],vaddr_byte, paddr_byte,size,DWRITE, 0x0);
		}
		if (size)
			warn("Scatter byte write address va: %x pa: %llx data: %x", vaddr_byte, paddr_byte, env->vtcm_log.data.ub[i]);
    }


    env->ver_cur_slot = slot_tmp;
#endif




    return;
}

void mem_vector_gather_finish(CPUHexagonState *env, int slot)
{
    // Gather Loads
    int i;

    // FIXME: Move this to ext.c at commit time
    for (i = 0; i < fVECSIZE(); i++)
    {
        if ( env->vtcm_log.mask.ub[i] )
        {
#ifdef FIXME
            if (ARCH_OPT_TH(vtcm_poison_enable) && !env->bq_on)
            {
              //                read_vtcm_poison_scatter_gather_byte(env, env->vtcm_log.pa[i]);
                enlist_byte(env, env->vtcm_log.pa[i], POISON, 1); //rw=1, read-poison
            }
#endif
        }
#ifdef FIXME
		if ( ARCH_OPT_TH(mmvec_network_addr_log2))
		{
			fprintf(ARCH_OPT_TH(mmvec_network_addr_log2), "pa: %llx enable: %d\n", env->vtcm_log.pa[i],  env->vtcm_log.mask.ub[i]);
		}
#endif
    }

	memcpy(env->mem_access[slot].cdata, &env->vtcm_log.offsets.ub[0], 256);

#ifdef VERIFICATION
    int slot_tmp = env->ver_cur_slot;
    env->ver_cur_slot = slot;
#endif

#ifdef FIXME
	if (!env->bq_on) {
		MEMTRACE_LD(env, env->Regs[REG_PC], env->vtcm_log.va_base, env->vtcm_log.pa_base, 1, VGATHER, 0xfeedfacedeadbeefULL);

		if (env->processor_ptr->options->testgen_mode)
		{
			MEMTRACE_LD(env, env->Regs[REG_PC], env->vtcm_log.va_base, env->vtcm_log.pa_base, 1, VGATHER, 0xfeedfacedeadbeefULL);
		} else {
			MEMTRACE_LD(env, env->Regs[REG_PC], env->vtcm_log.va_base, env->vtcm_log.pa_base, 0, DREAD, 0xfeedfacedeadbeefULL);
		}
	}
#endif
#ifdef VERIFICATION
    for (i = 0; i < fVECSIZE(); i++)
    {
        int size = env->vtcm_log.mask.ub[i] ? 1 : 0;
        paddr_t paddr_byte = env->vtcm_log.pa[i];
        vaddr_t vaddr_byte = (paddr_byte - env->vtcm_log.pa_base) + env->vtcm_log.va_base;
		if (env->processor_ptr->options->testgen_mode)
		{
			MEMTRACE_LD(env, env->Regs[REG_PC], vaddr_byte, paddr_byte, size, DREAD, 0xfeedfacedeadbeefULL);
		}
		if (size)
			warn("Gather byte read address va: %x pa: %llx data: %x", vaddr_byte, paddr_byte, env->vtcm_log.data.ub[i]);
       
    }

    env->ver_cur_slot = slot_tmp;
#endif


}
