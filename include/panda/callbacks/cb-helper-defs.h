/* PANDABEGINCOMMENT
 * 
 * Authors:
 *  Tim Leek               tleek@ll.mit.edu
 *  Ryan Whelan            rwhelan@ll.mit.edu
 *  Joshua Hodosh          josh.hodosh@ll.mit.edu
 *  Michael Zhivich        mzhivich@ll.mit.edu
 *  Brendan Dolan-Gavitt   brendandg@gatech.edu
 * 
 * This work is licensed under the terms of the GNU GPL, version 2. 
 * See the COPYING file in the top-level directory. 
 * 
PANDAENDCOMMENT */
DEF_HELPER_1(panda_insn_exec, void, tl)
DEF_HELPER_1(panda_after_insn_exec, void, tl)

#if defined(TARGET_ARM) || defined(TARGET_MIPS)
DEF_HELPER_1(panda_guest_hypercall, void, env)
#endif
