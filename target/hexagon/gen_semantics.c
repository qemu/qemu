/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
        return 1;
    }
    outfile = fopen(argv[1], "w");
    if (outfile == NULL) {
        fprintf(stderr, "Cannot open %s for writing\n", argv[1]);
        return 1;
    }

/*
 * Process the instruction definitions
 *     Scalar core instructions have the following form
 *         Q6INSN(A2_add,"Rd32=add(Rs32,Rt32)",ATTRIBS(),
 *         "Add 32-bit registers",
 *         { RdV=RsV+RtV;})
 */
#define Q6INSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
    do { \
        fprintf(outfile, "SEMANTICS( \\\n" \
                         "    \"%s\", \\\n" \
                         "    %s, \\\n" \
                         "    \"\"\"%s\"\"\" \\\n" \
                         ")\n", \
                #TAG, STRINGIZE(BEH), STRINGIZE(SEM)); \
        fprintf(outfile, "ATTRIBUTES( \\\n" \
                         "    \"%s\", \\\n" \
                         "    \"%s\" \\\n" \
                         ")\n", \
                #TAG, STRINGIZE(ATTRIBS)); \
    } while (0);
#include "imported/allidefs.def"
#undef Q6INSN

/*
 * Process the macro definitions
 *     Macros definitions have the following form
 *         DEF_MACRO(
 *             fLSBNEW0,
 *             predlog_read(thread,0),
 *             ()
 *         )
 * The important part here is the attributes.  Whenever an instruction
 * invokes a macro, we add the macro's attributes to the instruction.
 */
#define DEF_MACRO(MNAME, BEH, ATTRS) \
    fprintf(outfile, "MACROATTRIB( \\\n" \
                     "    \"%s\", \\\n" \
                     "    \"\"\"%s\"\"\", \\\n" \
                     "    \"%s\" \\\n" \
                     ")\n", \
            #MNAME, STRINGIZE(BEH), STRINGIZE(ATTRS));
#include "imported/macros.def"
#undef DEF_MACRO

    fclose(outfile);
    return 0;
}
