/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This program generates the semantics file that is processed by
 * the do_qemu.py script.  We use the C preporcessor to manipulate the
 * files imported from the Hexagon architecture library.
 */

#include <stdio.h>
#define STRINGIZE(X) #X

int main(int argc, char *argv[])
{
    FILE *outfile;

    if (argc != 2) {
        fprintf(stderr, "Usage: gen_semantics ouptputfile\n");
        return -1;
    }
    outfile = fopen(argv[1], "w");
    if (outfile == NULL) {
        fprintf(stderr, "Cannot open %s for writing\n", argv[1]);
        return -1;
    }

/*
 * Process the instruction definitions
 *     Scalar core instructions have the following form
 *         Q6INSN(A2_add,"Rd32=add(Rs32,Rt32)",ATTRIBS(),
 *         "Add 32-bit registers",
 *         { RdV=RsV+RtV;})
 *     HVX instructions have the following form
 *         EXTINSN(V6_vinsertwr, "Vx32.w=vinsert(Rt32)",
 *         ATTRIBS(A_EXTENSION,A_CVI,A_CVI_VX,A_CVI_LATE,A_NOTE_MPY_RESOURCE),
 *         "Insert Word Scalar into Vector",
 *         VxV.uw[0] = RtV;)
 */
#define Q6INSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
    do { \
        fprintf(outfile, "SEMANTICS(\"%s\",%s,\"\"\"%s\"\"\")\n", \
                #TAG, STRINGIZE(BEH), STRINGIZE(SEM)); \
        fprintf(outfile, "ATTRIBUTES(\"%s\",\"%s\")\n", \
                #TAG, STRINGIZE(ATTRIBS)); \
    } while (0);
#define EXTINSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
    do { \
        fprintf(outfile, "EXT_SEMANTICS(\"%s\",\"%s\",%s,\"\"\"%s\"\"\")\n", \
                EXTSTR, #TAG, STRINGIZE(BEH), STRINGIZE(SEM)); \
        fprintf(outfile, "ATTRIBUTES(\"%s\",\"%s\")\n", \
                #TAG, STRINGIZE(ATTRIBS)); \
    } while (0);
#include "imported/allidefs.def"
#undef Q6INSN
#undef EXTINSN

/*
 * Process the macro definitions
 *     Macros definitions have the following form
 *         DEF_MACRO(
 *             fLSBNEW0,,
 *             "P0.new[0]",
 *             "Least significant bit of new P0",
 *             predlog_read(thread,0),
 *             (A_DOTNEW,A_IMPLICIT_READS_P0)
 *         )
 * The important part here is the attributes.  Whenever an instruction
 * invokes a macro, we add the macro's attributes to the instruction.
 */
#define DEF_MACRO(MNAME, PARAMS, SDESC, LDESC, BEH, ATTRS) \
    fprintf(outfile, "MACROATTRIB(\"%s\",\"\"\"%s\"\"\",\"%s\")\n", \
            #MNAME, STRINGIZE(BEH), STRINGIZE(ATTRS));
#include "imported/macros.def"
#undef DEF_MACRO

/*
 * Process the macros for HVX
 */
#define DEF_MACRO(MNAME, PARAMS, SDESC, LDESC, BEH, ATTRS) \
    fprintf(outfile, "MACROATTRIB(\"%s\",\"\"\"%s\"\"\",\"%s\",\"%s\")\n", \
            #MNAME, STRINGIZE(BEH), STRINGIZE(ATTRS), EXTSTR);
#include "imported/allext_macros.def"
#undef DEF_MACRO

    fclose(outfile);
    return 0;
}
