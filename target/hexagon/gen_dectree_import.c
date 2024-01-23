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
 * This program generates the encodings file that is processed by
 * the dectree.py script to produce the decoding tree.  We use the C
 * preprocessor to manipulate the files imported from the Hexagon
 * architecture library.
 */
#include <stdio.h>
#include <string.h>
#include "opcodes.h"

#define STRINGIZE(X)    #X

const char * const opcode_names[] = {
#define OPCODE(IID) STRINGIZE(IID)
#include "opcodes_def_generated.h.inc"
    NULL
#undef OPCODE
};

/*
 * Process the instruction definitions
 *     Scalar core instructions have the following form
 *         Q6INSN(A2_add,"Rd32=add(Rs32,Rt32)",ATTRIBS(),
 *         "Add 32-bit registers",
 *         { RdV=RsV+RtV;})
 *     HVX instructions have the following form
 *         EXTINSN(V6_vinsertwr, "Vx32.w=vinsert(Rt32)",
 *         ATTRIBS(A_EXTENSION,A_CVI,A_CVI_VX,A_CVI_LATE),
 *         "Insert Word Scalar into Vector",
 *         VxV.uw[0] = RtV;)
 */
const char * const opcode_syntax[XX_LAST_OPCODE] = {
#define Q6INSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
   [TAG] = BEH,
#define EXTINSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
   [TAG] = BEH,
#include "imported/allidefs.def"
#undef Q6INSN
#undef EXTINSN
};

const OpcodeEncoding opcode_encodings[] = {
#define DEF_ENC32(TAG, ENCSTR) \
    [TAG] = { .encoding = ENCSTR },
#define DEF_ENC_SUBINSN(TAG, CLASS, ENCSTR) \
    [TAG] = { .encoding = ENCSTR, .enc_class = CLASS },
#define DEF_EXT_ENC(TAG, CLASS, ENCSTR) \
    [TAG] = { .encoding = ENCSTR, .enc_class = CLASS },
#include "imported/encode.def"
#undef DEF_ENC32
#undef DEF_ENC_SUBINSN
#undef DEF_EXT_ENC
};

static const char * const opcode_enc_class_names[XX_LAST_ENC_CLASS] = {
    "NORMAL",
    "16BIT",
    "SUBINSN_A",
    "SUBINSN_L1",
    "SUBINSN_L2",
    "SUBINSN_S1",
    "SUBINSN_S2",
    "EXT_noext",
    "EXT_mmvec",
};

static const char *get_opcode_enc(int opcode)
{
    const char *tmp = opcode_encodings[opcode].encoding;
    if (tmp == NULL) {
        tmp = "MISSING ENCODING";
    }
    return tmp;
}

static const char *get_opcode_enc_class(int opcode)
{
    const char *tmp = opcode_encodings[opcode].encoding;
    if (tmp == NULL) {
        const char *test = "V6_";        /* HVX */
        const char *name = opcode_names[opcode];
        if (strncmp(name, test, strlen(test)) == 0) {
            return "EXT_mmvec";
        }
    }
    return opcode_enc_class_names[opcode_encodings[opcode].enc_class];
}

static void gen_iset_table(FILE *out)
{
    int i;

    fprintf(out, "iset = {\n");
    for (i = 0; i < XX_LAST_OPCODE; i++) {
        fprintf(out, "\t\'%s\' : {\n", opcode_names[i]);
        fprintf(out, "\t\t\'tag\' : \'%s\',\n", opcode_names[i]);
        fprintf(out, "\t\t\'syntax\' : \'%s\',\n", opcode_syntax[i]);
        fprintf(out, "\t\t\'enc\' : \'%s\',\n", get_opcode_enc(i));
        fprintf(out, "\t\t\'enc_class\' : \'%s\',\n", get_opcode_enc_class(i));
        fprintf(out, "\t},\n");
    }
    fprintf(out, "};\n\n");
}

static void gen_tags_list(FILE *out)
{
    int i;

    fprintf(out, "tags = [\n");
    for (i = 0; i < XX_LAST_OPCODE; i++) {
        fprintf(out, "\t\'%s\',\n", opcode_names[i]);
    }
    fprintf(out, "];\n\n");
}

int main(int argc, char *argv[])
{
    FILE *outfile;

    if (argc != 2) {
        fprintf(stderr, "Usage: gen_dectree_import ouptputfile\n");
        return 1;
    }
    outfile = fopen(argv[1], "w");
    if (outfile == NULL) {
        fprintf(stderr, "Cannot open %s for writing\n", argv[1]);
        return 1;
    }

    gen_iset_table(outfile);
    gen_tags_list(outfile);

    fclose(outfile);
    return 0;
}
