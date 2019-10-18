/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


/*
 * printinsn.c
 * 
 * data tables generated automatically 
 * Maybe some functions too 
 * 
 * 
 */

#include <stdio.h>
#include <string.h>
#include "qemu/osdep.h"
#include "opcodes.h"
#include "printinsn.h"
#include "insn.h"
#include "regs.h"
//#include "decode.h"
//#include "../arch/external_api.h"

#if defined(NO_SILVER) || defined(DISASM_SILVER)
#define SKIP_SILVER 0
#else
#define SKIP_SILVER 1
#endif

const char *sreg2str(int reg);
const char *sreg2str(int reg)
{
	// FIXME: Can't return a stack allocated array
	static char buf[32];
#define DEF_REG(REG,DESCR,REGNAM,NREGS,VAL) \
	if ((reg < 16) && (reg < ((REG + NREGS) - (NUM_GEN_REGS + 32)))) { \
		const char *fmt = REGNAM; \
		sprintf(buf,fmt,reg-REG); \
		return buf; \
	}
#define DEF_GLOBAL_REG(...)		/* Nothing */
#define DEF_MMAP_REG(...) /* NOTHING */
#define DEF_REG_FIELD(...)		/* Nothing */
#define DEF_REG_MUTABILITY(...)	/* Nothing */
#define DEF_GLOBAL_REG_MUTABILITY(...)	/* Nothing */
#include "regs.def"
#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY

#define DEF_REG(...)			/* Nothing */
#define DEF_MMAP_REG(...) /* NOTHING */
#define DEF_GLOBAL_REG(REG,DESCR,REGNAM,NREGS,VAL) \
	if ((reg - 16) < ((REG + NREGS) - (0))) { \
		const char *fmt = REGNAM; \
		sprintf(buf,fmt,reg-REG); \
		return buf; \
	}
#define DEF_REG_FIELD(...)		/* Nothing */
#define DEF_REG_MUTABILITY(...)	/* Nothing */
#define DEF_GLOBAL_REG_MUTABILITY(...)	/* Nothing */
#include "regs.def"
#undef DEF_REG
#undef DEF_MMAP_REG
#undef DEF_GLOBAL_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY

	return "???";
}

const char *creg2str(int reg);
const char *creg2str(int reg)
{
	// FIXME: Can't return a stack allocated array
	static char buf[32];
#define DEF_REG(REG,DESCR,REGNAM,NREGS,VAL) \
	if ((reg >= (REG-NUM_GEN_REGS)) && (reg < (REG + NREGS - NUM_GEN_REGS))) { \
		const char *fmt = REGNAM; \
		sprintf(buf,fmt,reg-(REG-NUM_GEN_REGS)); \
		return buf; \
	}
#define DEF_GLOBAL_REG(...)		/* Nothing */
#define DEF_MMAP_REG(...) /* NOTHING */
#define DEF_REG_FIELD(...)		/* Nothing */
#define DEF_REG_MUTABILITY(...)	/* Nothing */
#define DEF_GLOBAL_REG_MUTABILITY(...)	/* Nothing */
#include "regs.def"
#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY

	return "???";
}

void snprintinsn(char *buf, int n, insn_t * insn)
{
	switch (insn->opcode) {
#define DEF_VECX_PRINTINFO(TAG,FMT,...) DEF_PRINTINFO(TAG,FMT,__VA_ARGS__)
#define DEF_PRINTINFO(TAG,FMT,...) \
		case TAG: \
			snprintf(buf,n,FMT,__VA_ARGS__);\
			break;
#include "printinsn.odef"
#undef DEF_ENCSTR
#undef IMMNO
#undef REGNO
	}
}

void fprintinsn(FILE * file, insn_t * insn)
{
	char buf[128];
	snprintinsn(buf, 127, insn);
	fprintf(file, "%s", buf);
}

/* Setting bits in the fields will print/not print some data
0: slot and tag information
1: EA and PA for memory accesses 
*/
#define SNPRINT_FIELD(R, SNFIELD)               \
    fEXTRACTU_BITS(R, 0x1, (SNFIELD))

#ifdef FIXME
static inline const char *ctype_str(xlate_info_t xinfo)
{
	if (xinfo.inner.cacheable || xinfo.outer.cacheable) return "cacheable";
	if (xinfo.memtype.arch_cacheable) return "architecturally cacheable but cache disabled";
	if (xinfo.memtype.device) return "device";
	return "uncached";
}
#else
#define ctype_str(x) __ctype_str()
static inline const char *__ctype_str(void)
{
	return "FIXME: ctype_str";
}
#endif

extern const size1u_t insn_timing_classes[];
extern char *timing_class_names[];

#ifdef FIXME
void snprint_add_trap1_info(char *buf, int n, opcode_t opcode, thread_t * thread)
{
	

	if ((opcode == J2_trap1) && (thread != NULL)) {
		switch (thread->trap1_info) {
			case TRAP1_VIRTINSN_RTE:
				strncat(buf, "// VIRTINSN RTE", n);	
				break;
			case TRAP1_VIRTINSN_SETIE:
				strncat(buf, "// VIRTINSN SETIE", n);	
				break;
			case TRAP1_VIRTINSN_GETIE:
				strncat(buf, "// VIRTINSN GETIE", n);	
				break;
			case TRAP1_VIRTINSN_SPSWAP:
				strncat(buf, "// VIRTINSN SPSWAP", n);	
				break;
			case TRAP1_NONE:
			case TRAP1:
				break;
			
		}			
	} 
}
#else
#define snprint_add_trap1_info(buf, n, opcode, Z) __snprint_add_trap1_info(buf, n, opcode)
void __snprint_add_trap1_info(char *buf, int n, opcode_t opcode);
void __snprint_add_trap1_info(char *buf, int n, opcode_t opcode)
{
	if (opcode == J2_trap1)
		strncat(buf, "FIXME: snprint_add_trap1_info", n);
}
#endif

#ifdef FIXME
void snprint_a_pkt_fields(char *buf, int n, packet_t * pkt, thread_t * thread, size4u_t fields)
{
	char tmpbuf[128];
	buf[0] = '\0';
	int i, slot;
	if (pkt == NULL) {
		snprintf(buf, n, "<printpkt: NULL ptr>");
		return;
	}
    /* snprintf(tmpbuf, n, "PC_VA=%x:PC_PA=%llx\n", pkt->PC_VA, pkt->PC_PA); */
    /* strncat(buf, tmpbuf, n); */
	if (pkt->num_insns > 1) {
		strncat(buf, "\n{\n", n);
	}
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].part1)
			continue;
		snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
		strncat(buf, "\t", n);
		strncat(buf, tmpbuf, n);
		if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
			strncat(buf, " //subinsn", n);
		}
		if (pkt->insn[i].extension_valid) {
			strncat(buf, " //constant extended", n);
		}

        slot = pkt->insn[i].slot;
        if(SNPRINT_FIELD(fields, SNFIELD_SLOTTAG)) {
			if (!(SKIP_SILVER && GET_ATTRIB(pkt->insn[i].opcode,A_VECX))) {
            snprintf(tmpbuf, 127, " //slot=%d:tag=%s:%s", 
                     slot,
                     opcode_names[pkt->insn[i].opcode],
		     timing_class_names[insn_timing_classes[pkt->insn[i].opcode]]);
            strncat(buf, tmpbuf, n);
			}
        }

		if ((pkt->slot_cancelled & (1 << slot)) 
			&& (pkt->insn[i].opcode != J2_endloop0)
			&& (pkt->insn[i].opcode != J2_endloop1)
			&& (pkt->insn[i].opcode != J2_endloop01)) {
			strncat(buf, " //cancelled", n);
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD) ||
				   GET_ATTRIB(pkt->insn[i].opcode, A_STORE) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_DCFETCH) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_ICINVA) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_COPBYADDRESS)) {
            if(SNPRINT_FIELD(fields, SNFIELD_EAPA)) {
                if ((thread != NULL) 
				&& (!(SKIP_SILVER 
				  && GET_ATTRIB(pkt->insn[i].opcode,A_VECX)))) {
                    snprintf(tmpbuf, 127, " //VA=%x PA=%llx (%s)",
				thread->mem_access[slot].vaddr,
				thread->mem_access[slot].paddr,
				ctype_str(thread->mem_access[slot].xlate_info)
				);
                    strncat(buf, tmpbuf, n);
                }
			}
		}
		snprint_add_trap1_info(buf, n, pkt->insn[i].opcode, thread);
		 
		strncat(buf, "\n", n);
	}
	if (pkt->num_insns > 1) {
		strncat(buf, "}\n", n);
	}
}
#else
#define snprint_a_pkt_fields(buf, n, pkt, T, fields) __snprint_a_pkt(buf, n, pkt, fields
void __snprint_a_pkt_fields(char *buf, int n, packet_t * pkt, size4u_t fields);
void __snprint_a_pkt_fields(char *buf, int n, packet_t * pkt, size4u_t fields)
{
	char tmpbuf[128];
	buf[0] = '\0';
	int i, slot;
	if (pkt == NULL) {
		snprintf(buf, n, "<printpkt: NULL ptr>");
		return;
	}
    /* snprintf(tmpbuf, n, "PC_VA=%x:PC_PA=%llx\n", pkt->PC_VA, pkt->PC_PA); */
    /* strncat(buf, tmpbuf, n); */
	if (pkt->num_insns > 1) {
		strncat(buf, "\n{\n", n);
	}
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].part1)
			continue;
		snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
		strncat(buf, "\t", n);
		strncat(buf, tmpbuf, n);
		if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
			strncat(buf, " //subinsn", n);
		}
		if (pkt->insn[i].extension_valid) {
			strncat(buf, " //constant extended", n);
		}

        slot = pkt->insn[i].slot;
#ifdef FIXME
        if(SNPRINT_FIELD(fields, SNFIELD_SLOTTAG)) {
			if (!(SKIP_SILVER && GET_ATTRIB(pkt->insn[i].opcode,A_VECX))) {
            snprintf(tmpbuf, 127, " //slot=%d:tag=%s:%s", 
                     slot,
                     opcode_names[pkt->insn[i].opcode],
		     timing_class_names[insn_timing_classes[pkt->insn[i].opcode]]);
            strncat(buf, tmpbuf, n);
			}
        }
#endif

		if ((pkt->slot_cancelled & (1 << slot)) 
			&& (pkt->insn[i].opcode != J2_endloop0)
			&& (pkt->insn[i].opcode != J2_endloop1)
			&& (pkt->insn[i].opcode != J2_endloop01)) {
			strncat(buf, " //cancelled", n);
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD) ||
				   GET_ATTRIB(pkt->insn[i].opcode, A_STORE) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_DCFETCH) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_ICINVA) ||
                   GET_ATTRIB(pkt->insn[i].opcode, A_COPBYADDRESS)) {
#ifdef FIXME
            if(SNPRINT_FIELD(fields, SNFIELD_EAPA)) {
                if ((thread != NULL) 
				&& (!(SKIP_SILVER 
				  && GET_ATTRIB(pkt->insn[i].opcode,A_VECX)))) {
                    snprintf(tmpbuf, 127, " //VA=%x PA=%llx (%s)",
				thread->mem_access[slot].vaddr,
				thread->mem_access[slot].paddr,
				ctype_str(thread->mem_access[slot].xlate_info)
				);
                    strncat(buf, tmpbuf, n);
                }
			}
#endif
		}
		snprint_add_trap1_info(buf, n, pkt->insn[i].opcode, thread);
		 
		strncat(buf, "\n", n);
	}
	if (pkt->num_insns > 1) {
		strncat(buf, "}\n", n);
	}
}
#endif


#ifdef FIXME
void gdb_print_pkt(packet_t * pkt, thread_t * thread) {
	char buf[1024];
	snprint_a_pkt(buf, 1024, pkt, thread);
	printf("%s\n", buf);
}
#else
#define gdb_print_pkt(pkt, T) __gdb_print_pkt(pkt)
void __gdb_print_pkt(packet_t * pkt);
void __gdb_print_pkt(packet_t * pkt) {
	char buf[1024];
	snprint_a_pkt(buf, 1024, pkt);
	printf("%s\n", buf);
}
#endif


#ifdef FIXME
void snprint_a_pkt(char *buf, int n, packet_t * pkt, thread_t * thread)
{
	char tmpbuf[128];
	buf[0] = '\0';
	int i, slot, opcode;
	if (pkt == NULL) {
		snprintf(buf, n, "<printpkt: NULL ptr>");
		return;
	}
	if (pkt->num_insns > 1) {
		strncat(buf, "\n{\n", n);
	}
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].part1)
			continue;
		snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
		strncat(buf, "\t", n);
		strncat(buf, tmpbuf, n);
		if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
			strncat(buf, " //subinsn", n);
		}
		if (pkt->insn[i].extension_valid) {
			strncat(buf, " //constant extended", n);
		}
		slot = pkt->insn[i].slot;
        opcode = pkt->insn[i].opcode;
		if (!(SKIP_SILVER && GET_ATTRIB(pkt->insn[i].opcode,A_VECX))) {
			snprintf(tmpbuf, 127, " //slot=%d:tag=%s", slot, opcode_names[opcode]);
			strncat(buf, tmpbuf, n);
		}

		if (pkt->slot_cancelled & (1 << slot)) {
			strncat(buf, " //cancelled", n);
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD) 
				|| GET_ATTRIB(pkt->insn[i].opcode, A_STORE)
				|| GET_ATTRIB(pkt->insn[i].opcode, A_DCFETCH)
				|| GET_ATTRIB(pkt->insn[i].opcode, A_COPBYADDRESS)) {
			if (thread != NULL) {
				if (pkt->insn[i].opcode == L6_memcpy) {
					snprintf(tmpbuf, 127, " //DST EA=0x%08x PA=0x%09llx (%s) SRC: EA=0x%08x PA=0x%09llx (%s)",
						 thread->mem_access[0].vaddr,
						 thread->mem_access[0].paddr,
						 ctype_str(thread->mem_access[0].xlate_info),
						 thread->mem_access[1].vaddr,
						 thread->mem_access[1].paddr,
						 ctype_str(thread->mem_access[1].xlate_info));		 
				} else {
					snprintf(tmpbuf, 127, " //EA=0x%08x PA=0x%09llx (%s)",
							 thread->mem_access[slot].vaddr,
							 thread->mem_access[slot].paddr,
							ctype_str(thread->mem_access[slot].xlate_info));
				}
				strncat(buf, tmpbuf, n);
			}
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_COPBYIDX)) {
			if (thread != NULL) {
				snprintf(tmpbuf, 127, " //regval=0x%08x ",
					thread->mem_access[slot].vaddr);
				strncat(buf, tmpbuf, n);
			}
		}
		snprint_add_trap1_info(buf, n, pkt->insn[i].opcode, thread);
		 
		strncat(buf, "\n", n);
	}
	if (pkt->num_insns > 1) {
		strncat(buf, "}\n", n);
	}
}
#else
void snprint_a_pkt(char *buf, int n, packet_t * pkt)
{
	char tmpbuf[128];
	buf[0] = '\0';
	int i, slot, opcode;
	if (pkt == NULL) {
		snprintf(buf, n, "<printpkt: NULL ptr>");
		return;
	}
	if (pkt->num_insns > 1) {
		strncat(buf, "\n{\n", n);
	}
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].part1)
			continue;
		snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
		strncat(buf, "\t", n);
		strncat(buf, tmpbuf, n);
		if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
			strncat(buf, " //subinsn", n);
		}
		if (pkt->insn[i].extension_valid) {
			strncat(buf, " //constant extended", n);
		}
		slot = pkt->insn[i].slot;
	        opcode = pkt->insn[i].opcode;
		snprintf(tmpbuf, 127, " //slot=%d:tag=%s", slot, opcode_names[opcode]);
		strncat(buf, tmpbuf, n);

		if (pkt->slot_cancelled & (1 << slot)) {
			strncat(buf, " //cancelled", n);
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_LOAD) 
				|| GET_ATTRIB(pkt->insn[i].opcode, A_STORE)
				|| GET_ATTRIB(pkt->insn[i].opcode, A_DCFETCH)
				|| GET_ATTRIB(pkt->insn[i].opcode, A_COPBYADDRESS)) {
#ifdef FIXME
			if (thread != NULL) {
				if (pkt->insn[i].opcode == L6_memcpy) {
					snprintf(tmpbuf, 127, " //DST EA=0x%08x PA=0x%09llx (%s) SRC: EA=0x%08x PA=0x%09llx (%s)",
						 thread->mem_access[0].vaddr,
						 thread->mem_access[0].paddr,
						 ctype_str(thread->mem_access[0].xlate_info),
						 thread->mem_access[1].vaddr,
						 thread->mem_access[1].paddr,
						 ctype_str(thread->mem_access[1].xlate_info));		 
				} else {
					snprintf(tmpbuf, 127, " //EA=0x%08x PA=0x%09llx (%s)",
							 thread->mem_access[slot].vaddr,
							 thread->mem_access[slot].paddr,
							ctype_str(thread->mem_access[slot].xlate_info));
				}
				strncat(buf, tmpbuf, n);
			}
#endif
		} else if (GET_ATTRIB(pkt->insn[i].opcode, A_COPBYIDX)) {
#ifdef FIXME
			if (thread != NULL) {
				snprintf(tmpbuf, 127, " //regval=0x%08x ",
					thread->mem_access[slot].vaddr);
				strncat(buf, tmpbuf, n);
			}
#endif
		}
		snprint_add_trap1_info(buf, n, pkt->insn[i].opcode, thread);
		 
		strncat(buf, "\n", n);
	}
	if (pkt->num_insns > 1) {
		strncat(buf, "}\n", n);
	}
}
#endif

#ifdef FIXME
void snprint_a_pkt_tags(char *buf, int n, packet_t * pkt, thread_t * thread)
#else
#define snprint_a_pkt_tags(buf, n, pkt, T) __snprint_a_pkt(buf, n, pkt)
void __snprint_a_pkt_tags(char *buf, int n, packet_t * pkt);
void __snprint_a_pkt_tags(char *buf, int n, packet_t * pkt)
#endif
{
	char tmpbuf[128];
	buf[0] = '\0';
	int i, opcode;
	if (pkt == NULL) {
		snprintf(buf, n, "<printpkt: NULL ptr>");
		return;
	}
	for (i = 0; i < pkt->num_insns; i++) {
		if (pkt->insn[i].part1)
			continue;
        opcode = pkt->insn[i].opcode;
        snprintf(tmpbuf, 127, "%s:", opcode_names[opcode]);
		strncat(buf, tmpbuf, n);
	}
    strncat(buf, "\n", n);
}

void snprint_an_insn_tag(char *buf, int n, insn_t* insn)
{
	char tmpbuf[128];
	buf[0] = '\0';
	int opcode;
	if (insn == NULL) {
		snprintf(buf, n, "<print_insn_tag: NULL ptr>");
		return;
	}
        opcode = insn->opcode;
        snprintf(tmpbuf, 127, "%s", opcode_names[opcode]);
	strncat(buf, tmpbuf, n);
}

#ifdef FIXME
void
snprintpkt(char *buf, int n, processor_t * proc, int tnum, size4u_t PC)
{
	thread_t *thread = proc->thread[tnum];
	packet_t *pkt = thread->last_pkt;
	if ((pkt != NULL) && (pkt->PC_PA & 0x3ff) == (PC & 0x3ff)) {
		snprint_a_pkt(buf, n, pkt, proc->thread[tnum]);
	} else {
		strncat(buf, "not last pkt!", n);
	}
}

void snprint_last_pkt_fields(char *buf, int n, processor_t * proc, int tnum, size4u_t fields)
{
	thread_t *thread = proc->thread[tnum];
	packet_t *pkt = thread->last_pkt_trace;
#ifdef VERIFICATION
	if (thread->last_pkt == NULL) {
		pkt = &thread->ver_last_pkt;
	}
#endif
//      char tmpbuf[128];
	buf[0] = '\0';
//      int i;
	if (pkt != NULL) {
		snprint_a_pkt_fields(buf, n, pkt, proc->thread[tnum], fields);
	} else {
		strncat(buf, "<Can't find last packet>", n);
	}
}


void snprint_last_verif_pkt(char *buf, int n, processor_t * proc, int tnum)
{
	thread_t *thread = proc->thread[tnum];
	packet_t *pkt = NULL;
#ifdef VERIFICATION
	pkt = &thread->ver_last_pkt;
#endif
	buf[0] = '\0';
	if (pkt != NULL) {
		snprint_a_pkt(buf, n, pkt, thread);
	} else {
		strncat(buf, "<Can't find last packet>", n);
	}
}


void snprint_last_pkt(char *buf, int n, processor_t * proc, int tnum)
{
	thread_t *thread = proc->thread[tnum];
	packet_t *pkt = thread->last_pkt_trace;
#ifdef VERIFICATION
	if (thread->last_pkt == NULL) {
		pkt = &thread->ver_last_pkt;
	}
#endif
//      char tmpbuf[128];
	buf[0] = '\0';
//      int i;
	if (pkt != NULL) {
		snprint_a_pkt(buf, n, pkt, proc->thread[tnum]);
	} else {
		strncat(buf, "<Can't find last packet>", n);
	}
}

void arch_snprint_last_pkt(char *buf, int n, processor_t * proc, int tnum)
{
	snprint_last_pkt(buf, n, proc, tnum);
}

void arch_snprint_tags_only(char *buf, int n, processor_t * proc, int tnum)
{
	thread_t *thread = proc->thread[tnum];
	packet_t *pkt = thread->last_pkt_trace;
	buf[0] = '\0';
	if (pkt != NULL) {
		snprint_a_pkt_tags(buf, n, pkt, proc->thread[tnum]);
	} else {
		strncat(buf, "<Can't find last packet>", n);
	}
}

int arch_snprint_a_pkt(char *buf, int n, processor_t * proc, int tnum, size4u_t *encodings)
{
	packet_t decode_pkt;
	packet_t *pkt;
	thread_t *thread = proc->thread[tnum];
	if ((pkt = decode_this(thread,encodings,&decode_pkt)) == NULL) {
		strncpy(buf,"INVALID: packet decode error.",n);
		return -1;
	}
	snprint_a_pkt(buf,n,pkt,thread);
	return pkt->encod_pkt_size_in_bytes;
}


void arch_snprint_last_pkt_fields(char *buf, int n, processor_t * proc, int tnum, size4u_t fields)
{
	snprint_last_pkt_fields(buf, n, proc, tnum, fields);
}


size4u_t arch_get_last_pkt_pcva(processor_t * proc, int tnum)
{
	if (proc->thread[tnum]->last_pkt_trace == NULL) {
		return 0;
	}
	return (proc->thread[tnum]->last_pkt_pcva);
}
#endif


