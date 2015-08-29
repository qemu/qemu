/*
 * DSP56300 disassembly routines
 *
 * Copyright (c) 2015 espes
 *
 * Adapted from Hatari DSP M56001 emulation
 * (C) 2003-2008 ARAnyM developer team
 * Adaption to Hatari (C) 2008 by Thomas Huth
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

typedef void (*dis_func_t)(dsp_core_t* dsp);


static const char *registers_name[64]={
    "","","","",
    "x0","x1","y0","y1",
    "a0","b0","a2","b2",
    "a1","b1","a","b",
    
    "r0","r1","r2","r3",
    "r4","r5","r6","r7",
    "n0","n1","n2","n3",
    "n4","n5","n6","n7",

    "m0","m1","m2","m3",
    "m4","m5","m6","m7",
    "","","","",
    "","","","",

    "","","","",
    "","","","",
    "","sr","omr","sp",
    "ssh","ssl","la","lc"
};


static const char* disasm_opcodes_alu[256] = {
    /* 0x00 - 0x3f */
    "move"     , "tfr b,a", "addr b,a", "tst a", "undefined", "cmp b,a"  , "subr b,a", "cmpm b,a",
    "undefined", "tfr a,b", "addr a,b", "tst b", "undefined", "cmp a,b"  , "subr a,b", "cmpm a,b",
    "add b,a"  , "rnd a"  , "addl b,a", "clr a", "sub b,a"  , "undefined", "subl b,a", "not a",
    "add a,b"  , "rnd b"  , "addl a,b", "clr b", "sub a,b"  , "max a,b", "subl a,b", "not b",
    "add x,a"  , "adc x,a", "asr a" , "lsr a", "sub x,a"  , "sbc x,a"  , "abs a" , "ror a",
    "add x,b"  , "adc x,b", "asr b" , "lsr b", "sub x,b"  , "sbc x,b"  , "abs b" , "ror b",
    "add y,a"  , "adc y,a", "asl a" , "lsl a", "sub y,a"  , "sbc y,a"  , "neg a" , "rol a",
    "add y,b"  , "adc y,b", "asl b" , "lsl b", "sub y,b"  , "sbc y,b"  , "neg b" , "rol b",
    
    /* 0x40 - 0x7f */
    "add x0,a", "tfr x0,a", "or x0,a", "eor x0,a", "sub x0,a", "cmp x0,a", "and x0,a", "cmpm x0,a",
    "add x0,b", "tfr x0,b", "or x0,b", "eor x0,b", "sub x0,b", "cmp x0,b", "and x0,b", "cmpm x0,b",
    "add y0,a", "tfr y0,a", "or y0,a", "eor y0,a", "sub y0,a", "cmp y0,a", "and y0,a", "cmpm y0,a",
    "add y0,b", "tfr y0,b", "or y0,b", "eor y0,b", "sub y0,b", "cmp y0,b", "and y0,b", "cmpm y0,b",
    "add x1,a", "tfr x1,a", "or x1,a", "eor x1,a", "sub x1,a", "cmp x1,a", "and x1,a", "cmpm x1,a",
    "add x1,b", "tfr x1,b", "or x1,b", "eor x1,b", "sub x1,b", "cmp x1,b", "and x1,b", "cmpm x1,b",
    "add y1,a", "tfr y1,a", "or y1,a", "eor y1,a", "sub y1,a", "cmp y1,a", "and y1,a", "cmpm y1,a",
    "add y1,b", "tfr y1,b", "or y1,b", "eor y1,b", "sub y1,b", "cmp y1,b", "and y1,b", "cmpm y1,b",

    /* 0x80 - 0xbf */
    "mpy +x0,x0,a", "mpyr +x0,x0,a", "mac +x0,x0,a", "macr +x0,x0,a", "mpy -x0,x0,a", "mpyr -x0,x0,a", "mac -x0,x0,a", "macr -x0,x0,a",
    "mpy +x0,x0,b", "mpyr +x0,x0,b", "mac +x0,x0,b", "macr +x0,x0,b", "mpy -x0,x0,b", "mpyr -x0,x0,b", "mac -x0,x0,b", "macr -x0,x0,b",
    "mpy +y0,y0,a", "mpyr +y0,y0,a", "mac +y0,y0,a", "macr +y0,y0,a", "mpy -y0,y0,a", "mpyr -y0,y0,a", "mac -y0,y0,a", "macr -y0,y0,a",
    "mpy +y0,y0,b", "mpyr +y0,y0,b", "mac +y0,y0,b", "macr +y0,y0,b", "mpy -y0,y0,b", "mpyr -y0,y0,b", "mac -y0,y0,b", "macr -y0,y0,b",
    "mpy +x1,x0,a", "mpyr +x1,x0,a", "mac +x1,x0,a", "macr +x1,x0,a", "mpy -x1,x0,a", "mpyr -x1,x0,a", "mac -x1,x0,a", "macr -x1,x0,a",
    "mpy +x1,x0,b", "mpyr +x1,x0,b", "mac +x1,x0,b", "macr +x1,x0,b", "mpy -x1,x0,b", "mpyr -x1,x0,b", "mac -x1,x0,b", "macr -x1,x0,b",
    "mpy +y1,y0,a", "mpyr +y1,y0,a", "mac +y1,y0,a", "macr +y1,y0,a", "mpy -y1,y0,a", "mpyr -y1,y0,a", "mac -y1,y0,a", "macr -y1,y0,a",
    "mpy +y1,y0,b", "mpyr +y1,y0,b", "mac +y1,y0,b", "macr +y1,y0,b", "mpy -y1,y0,b", "mpyr -y1,y0,b", "mac -y1,y0,b", "macr -y1,y0,b",

    /* 0xc0 - 0xff */
    "mpy +x0,y1,a", "mpyr +x0,y1,a", "mac +x0,y1,a", "macr +x0,y1,a", "mpy -x0,y1,a", "mpyr -x0,y1,a", "mac -x0,y1,a", "macr -x0,y1,a",
    "mpy +x0,y1,b", "mpyr +x0,y1,b", "mac +x0,y1,b", "macr +x0,y1,b", "mpy -x0,y1,b", "mpyr -x0,y1,b", "mac -x0,y1,b", "macr -x0,y1,b",
    "mpy +y0,x0,a", "mpyr +y0,x0,a", "mac +y0,x0,a", "macr +y0,x0,a", "mpy -y0,x0,a", "mpyr -y0,x0,a", "mac -y0,x0,a", "macr -y0,x0,a",
    "mpy +y0,x0,b", "mpyr +y0,x0,b", "mac +y0,x0,b", "macr +y0,x0,b", "mpy -y0,x0,b", "mpyr -y0,x0,b", "mac -y0,x0,b", "macr -y0,x0,b",
    "mpy +x1,y0,a", "mpyr +x1,y0,a", "mac +x1,y0,a", "macr +x1,y0,a", "mpy -x1,y0,a", "mpyr -x1,y0,a", "mac -x1,y0,a", "macr -x1,y0,a",
    "mpy +x1,y0,b", "mpyr +x1,y0,b", "mac +x1,y0,b", "macr +x1,y0,b", "mpy -x1,y0,b", "mpyr -x1,y0,b", "mac -x1,y0,b", "macr -x1,y0,b",
    "mpy +y1,x1,a", "mpyr +y1,x1,a", "mac +y1,x1,a", "macr +y1,x1,a", "mpy -y1,x1,a", "mpyr -y1,x1,a", "mac -y1,x1,a", "macr -y1,x1,a",
    "mpy +y1,x1,b", "mpyr +y1,x1,b", "mac +y1,x1,b", "macr +y1,x1,b", "mpy -y1,x1,b", "mpyr -y1,x1,b", "mac -y1,x1,b", "macr -y1,x1,b"
};

static void dis_pm_0(dsp_core_t* dsp);
static void dis_pm_1(dsp_core_t* dsp);
static void dis_pm_2(dsp_core_t* dsp);
static void dis_pm_4(dsp_core_t* dsp);
static void dis_pm_8(dsp_core_t* dsp);

static const dis_func_t disasm_opcodes_parmove[16] = {
    dis_pm_0,
    dis_pm_1,
    dis_pm_2,
    dis_pm_2,
    dis_pm_4,
    dis_pm_4,
    dis_pm_4,
    dis_pm_4,

    dis_pm_8,
    dis_pm_8,
    dis_pm_8,
    dis_pm_8,
    dis_pm_8,
    dis_pm_8,
    dis_pm_8,
    dis_pm_8
};

static const char *registers_lmove[8] = {
    "a10",
    "b10",
    "x",
    "y",
    "a",
    "b",
    "ab",
    "ba"
};

static const char *ea_names[9] = {
    "(r%d)-n%d",    /* 000xxx */
    "(r%d)+n%d",    /* 001xxx */
    "(r%d)-",       /* 010xxx */
    "(r%d)+",       /* 011xxx */
    "(r%d)",        /* 100xxx */
    "(r%d+n%d)",    /* 101xxx */
    "$%04x",        /* 110000 */
    "-(r%d)",       /* 111xxx */
    "$%06x"     /* 110100 */
};

static const char *cc_name[16] = {
    "cc",
    "ge",
    "ne",
    "pl",
    "nn",
    "ec",
    "lc",
    "gt",
    
    "cs",
    "lt",
    "eq",
    "mi",
    "nr",
    "es",
    "ls",
    "le"
};


/**********************************
 *  Conditions code calculation
 **********************************/

static void dis_calc_cc(dsp_core_t* dsp, uint32_t cc_mode, char *dest)
{
    strcpy(dest, cc_name[cc_mode & BITMASK(4)]);
}

/**********************************
 *  Effective address calculation
 **********************************/

static int dis_calc_ea(dsp_core_t* dsp, uint32_t ea_mode, char *dest)
{
    int value, retour, numreg;

    value = (ea_mode >> 3) & BITMASK(3);
    numreg = ea_mode & BITMASK(3);
    retour = 0;
    switch (value) {
        case 0:
            /* (Rx)-Nx */
            sprintf(dest, ea_names[value], numreg, numreg);
            break;
        case 1:
            /* (Rx)+Nx */
            sprintf(dest, ea_names[value], numreg, numreg);
            break;
        case 5:
            /* (Rx+Nx) */
            sprintf(dest, ea_names[value], numreg, numreg);
            break;
        case 2:
            /* (Rx)- */
            sprintf(dest, ea_names[value], numreg);
            break;
        case 3:
            /* (Rx)+ */
            sprintf(dest, ea_names[value], numreg);
            break;
        case 4:
            /* (Rx) */
            sprintf(dest, ea_names[value], numreg);
            break;
        case 7:
            /* -(Rx) */
            sprintf(dest, ea_names[value], numreg);
            break;
        case 6:
            dsp->disasm_cur_inst_len++;
            switch ((ea_mode >> 2) & 1) {
                case 0:
                    /* Absolute address */
                    sprintf(dest, ea_names[value], read_memory_p(dsp, dsp->pc+1));
                    break;
                case 1:
                    /* Immediate value */
                    sprintf(dest, ea_names[8], read_memory_p(dsp, dsp->pc+1));
                    retour = 1;
                    break;
            }
            break;
    }
    return retour;
}

/**********************************
 *  Non-parallel moves instructions
 **********************************/

static void dis_undefined(dsp_core_t* dsp)
{
    if (dsp->disasm_mode == DSP_DISASM_MODE) {
        /* In Disasm mode, display dc instruction_opcode */
        sprintf(dsp->disasm_str_instr, "dc $%06x", dsp->disasm_cur_inst);
    } else {
        /* In trace mode, display unknown instruction */
        sprintf(dsp->disasm_str_instr, "$%06x unknown instruction", dsp->disasm_cur_inst);
    }
}

static void dis_add_imm(dsp_core_t* dsp)
{
    uint32_t xx = (dsp->disasm_cur_inst >> 8) & BITMASK(6);
    uint32_t accname = ((dsp->disasm_cur_inst >> 3) & 1) ? DSP_REG_B : DSP_REG_A;
    sprintf(dsp->disasm_str_instr, "add #$%02x,%s", xx, registers_name[accname]);
}

static void dis_add_long(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t accname = ((dsp->disasm_cur_inst >> 3) & 1) ? DSP_REG_B : DSP_REG_A;
    sprintf(dsp->disasm_str_instr, "add #$%04x,%s", xxxx, registers_name[accname]);
}

static void dis_and_imm(dsp_core_t* dsp)
{
    uint32_t xx = (dsp->disasm_cur_inst >> 8) & BITMASK(6);
    uint32_t accname = ((dsp->disasm_cur_inst >> 3) & 1) ? DSP_REG_B : DSP_REG_A;
    sprintf(dsp->disasm_str_instr, "and #$%02x,%s", xx, registers_name[accname]);
}

static void dis_and_long(dsp_core_t* dsp)
{
    dsp->disasm_cur_inst_len++;
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    uint32_t accname = ((dsp->disasm_cur_inst >> 3) & 1) ? DSP_REG_B : DSP_REG_A;
    sprintf(dsp->disasm_str_instr, "and #$%04x,%s", xxxx, registers_name[accname]);
}

static void dis_andi(dsp_core_t* dsp)
{
    switch(dsp->disasm_cur_inst & BITMASK(2)) {
        case 0:
            sprintf(dsp->disasm_str_instr, "andi #$%02x,mr", (dsp->disasm_cur_inst>>8) & BITMASK(8));
            break;
        case 1:
            sprintf(dsp->disasm_str_instr, "andi #$%02x,ccr", (dsp->disasm_cur_inst>>8) & BITMASK(8));
            break;
        case 2:
            sprintf(dsp->disasm_str_instr, "andi #$%02x,omr", (dsp->disasm_cur_inst>>8) & BITMASK(8));
            break;
        default:
            break;
    }
}

static void dis_asl_imm(dsp_core_t* dsp)
{
    uint32_t S = (dsp->disasm_cur_inst >> 7) & 1;
    uint32_t D = dsp->disasm_cur_inst & 1;
    uint32_t ii = (dsp->disasm_cur_inst >> 1) & BITMASK(6);
    sprintf(dsp->disasm_str_instr, "asl #$%02x,%s,%s",
        ii,
        registers_name[S ? DSP_REG_B : DSP_REG_A],
        registers_name[D ? DSP_REG_B : DSP_REG_A]);
}

static void dis_asr_imm(dsp_core_t* dsp)
{
    uint32_t S = (dsp->disasm_cur_inst >> 7) & 1;
    uint32_t D = dsp->disasm_cur_inst & 1;
    uint32_t ii = (dsp->disasm_cur_inst >> 1) & BITMASK(6);
    sprintf(dsp->disasm_str_instr, "asr #$%02x,%s,%s",
        ii,
        registers_name[S ? DSP_REG_B : DSP_REG_A],
        registers_name[D ? DSP_REG_B : DSP_REG_A]);
}

static void dis_bcc_long(dsp_core_t* dsp) {
    dsp->disasm_cur_inst_len++;

    uint32_t cc_code = dsp->disasm_cur_inst & BITMASK(4);
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);

    char cond_name[16];
    dis_calc_cc(dsp, cc_code, cond_name);

    sprintf(dsp->disasm_str_instr, "b%s p:$%06x",
        cond_name, (dsp->pc + xxxx) & BITMASK(24));
}

static void dis_bcc_imm(dsp_core_t* dsp) {
    char cond_name[16];

    uint32_t cc_code = (dsp->disasm_cur_inst >> 12) & BITMASK(4);
    uint32_t xxx = (dsp->disasm_cur_inst & BITMASK(5))
                    + ((dsp->disasm_cur_inst & (BITMASK(4) << 6)) >> 1);

    dis_calc_cc(dsp, cc_code, cond_name);    

    sprintf(dsp->disasm_str_instr,"b%s p:$%06x",
        cond_name, (dsp->pc + dsp_signextend(9, xxx)) & BITMASK(24) );
}

static void dis_bchg_aa(dsp_core_t* dsp)
{
    /* bchg #n,x:aa */
    /* bchg #n,y:aa */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%04x",value);
    } else {
        sprintf(name,"x:$%04x",value);
    }

    sprintf(dsp->disasm_str_instr,"bchg #%d,%s", numbit, name);
}

static void dis_bchg_ea(dsp_core_t* dsp)
{
    /* bchg #n,x:ea */
    /* bchg #n,y:ea */
    char name[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(name,"y:%s",addr_name);
    } else {
        sprintf(name,"x:%s",addr_name);
    }

    sprintf(dsp->disasm_str_instr,"bchg #%d,%s", numbit, name);
}

static void dis_bchg_pp(dsp_core_t* dsp)
{
    /* bchg #n,x:pp */
    /* bchg #n,y:pp */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%06x",value+0xffffc0);
    } else {
        sprintf(name,"x:$%06x",value+0xffffc0);
    }

    sprintf(dsp->disasm_str_instr,"bchg #%d,%s", numbit, name);
}

static void dis_bchg_reg(dsp_core_t* dsp)
{
    /* bchg #n,R */
    uint32_t value, numbit;
    
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"bchg #%d,%s", numbit, registers_name[value]);
}

static void dis_bclr_aa(dsp_core_t* dsp)
{
    /* bclr #n,x:aa */
    /* bclr #n,y:aa */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%04x",value);
    } else {
        sprintf(name,"x:$%04x",value);
    }

    sprintf(dsp->disasm_str_instr,"bclr #%d,%s", numbit, name);
}

static void dis_bclr_ea(dsp_core_t* dsp)
{
    /* bclr #n,x:ea */
    /* bclr #n,y:ea */
    char name[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(name,"y:%s",addr_name);
    } else {
        sprintf(name,"x:%s",addr_name);
    }

    sprintf(dsp->disasm_str_instr,"bclr #%d,%s", numbit, name);
}

static void dis_bclr_pp(dsp_core_t* dsp)
{
    /* bclr #n,x:pp */
    /* bclr #n,y:pp */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%06x",value+0xffffc0);
    } else {
        sprintf(name,"x:$%06x",value+0xffffc0);
    }

    sprintf(dsp->disasm_str_instr,"bclr #%d,%s", numbit, name);
}

static void dis_bclr_reg(dsp_core_t* dsp)
{
    /* bclr #n,R */
    uint32_t value, numbit;
    
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"bclr #%d,%s", numbit, registers_name[value]);
}

static void dis_bra_long(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;
    sprintf(dsp->disasm_str_instr, "bra p:$%06x",
        (dsp->pc + xxxx) & BITMASK(24));
}

static void dis_bra_imm(dsp_core_t* dsp)
{
    uint32_t xxx = (dsp->disasm_cur_inst & BITMASK(5))
                    + ((dsp->disasm_cur_inst & (BITMASK(4) << 6)) >> 1);
    sprintf(dsp->disasm_str_instr, "bra p:$%04x",
        (dsp->pc + dsp_signextend(9, xxx)) & BITMASK(24) );
}

static void dis_brclr_pp(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;
    
    uint32_t memspace = (dsp->disasm_cur_inst>>6) & 1;
    uint32_t value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    uint32_t numbit = dsp->disasm_cur_inst & BITMASK(5);

    char name[16];
    if (memspace) {
        sprintf(name,"y:$%06x",value+0xffffc0);
    } else {
        sprintf(name,"x:$%06x",value+0xffffc0);
    }

    sprintf(dsp->disasm_str_instr,"brclr #%d,%s,p:$%06x",
        numbit, name, (dsp->pc + xxxx) & BITMASK(24) );
}

static void dis_brclr_reg(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    uint32_t numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr, "brclr #%d,%s,p:$%04x",
        numbit, registers_name[value], (dsp->pc + xxxx) & BITMASK(24));
}

static void dis_brset_pp(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;
    
    uint32_t memspace = (dsp->disasm_cur_inst>>6) & 1;
    uint32_t value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    uint32_t numbit = dsp->disasm_cur_inst & BITMASK(5);

    char name[16];
    if (memspace) {
        sprintf(name,"y:$%06x",value+0xffffc0);
    } else {
        sprintf(name,"x:$%06x",value+0xffffc0);
    }

    sprintf(dsp->disasm_str_instr,"brset #%d,%s,p:$%06x",
        numbit, name, (dsp->pc + xxxx) & BITMASK(24) );
}

static void dis_brset_reg(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    uint32_t numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr, "brset #%d,%s,p:$%04x",
        numbit, registers_name[value], (dsp->pc + xxxx) & BITMASK(24));
}

static void dis_bset_aa(dsp_core_t* dsp)
{
    /* bset #n,x:aa */
    /* bset #n,y:aa */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%04x",value);
    } else {
        sprintf(name,"x:$%04x",value);
    }

    sprintf(dsp->disasm_str_instr,"bset #%d,%s", numbit, name);
}

static void dis_bset_ea(dsp_core_t* dsp)
{
    /* bset #n,x:ea */
    /* bset #n,y:ea */
    char name[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(name,"y:%s",addr_name);
    } else {
        sprintf(name,"x:%s",addr_name);
    }

    sprintf(dsp->disasm_str_instr,"bset #%d,%s", numbit, name);
}

static void dis_bset_pp(dsp_core_t* dsp)
{
    /* bset #n,x:pp */
    /* bset #n,y:pp */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%06x",value+0xffffc0);
    } else {
        sprintf(name,"x:$%06x",value+0xffffc0);
    }

    sprintf(dsp->disasm_str_instr,"bset #%d,%s", numbit, name);
}

static void dis_bset_reg(dsp_core_t* dsp)
{
    /* bset #n,R */
    uint32_t value, numbit;
    
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"bset #%d,%s", numbit, registers_name[value]);
}

static void dis_bsr_long(dsp_core_t* dsp)
{
    dsp->disasm_cur_inst_len++;
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    sprintf(dsp->disasm_str_instr, "bsr p:$%06x",
        (dsp->pc + xxxx) & BITMASK(24));
}

static void dis_bsr_imm(dsp_core_t* dsp)
{
    uint32_t xxx = (dsp->disasm_cur_inst & BITMASK(5))
                 + ((dsp->disasm_cur_inst & (BITMASK(4) << 6)) >> 1);
    sprintf(dsp->disasm_str_instr, "bsr p:$%04x",
        (dsp->pc + dsp_signextend(9, xxx)) & BITMASK(24) );
}

static void dis_btst_aa(dsp_core_t* dsp)
{
    /* btst #n,x:aa */
    /* btst #n,y:aa */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%04x",value);
    } else {
        sprintf(name,"x:$%04x",value);
    }

    sprintf(dsp->disasm_str_instr,"btst #%d,%s", numbit, name);
}

static void dis_btst_ea(dsp_core_t* dsp)
{
    /* btst #n,x:ea */
    /* btst #n,y:ea */
    char name[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(name,"y:%s",addr_name);
    } else {
        sprintf(name,"x:%s",addr_name);
    }

    sprintf(dsp->disasm_str_instr,"btst #%d,%s", numbit, name);
}

static void dis_btst_pp(dsp_core_t* dsp)
{
    /* btst #n,x:pp */
    /* btst #n,y:pp */
    char name[16];
    uint32_t memspace, value, numbit;
    
    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(name,"y:$%06x",value+0xffffc0);
    } else {
        sprintf(name,"x:$%06x",value+0xffffc0);
    }

    sprintf(dsp->disasm_str_instr,"btst #%d,%s", numbit, name);
}

static void dis_btst_reg(dsp_core_t* dsp)
{
    /* btst #n,R */
    uint32_t value, numbit;
    
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"btst #%d,%s", numbit, registers_name[value]);
}

static void dis_cmp_imm(dsp_core_t* dsp) {
    uint32_t xx = (dsp->disasm_cur_inst >> 8) & BITMASK(6);
    uint32_t d = (dsp->disasm_cur_inst >> 3) & 1;

    sprintf(dsp->disasm_str_instr, "cmp #$%02x,%s",
        xx, registers_name[d ? DSP_REG_B : DSP_REG_A]);
}

static void dis_cmp_long(dsp_core_t* dsp) {
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t d = (dsp->disasm_cur_inst >> 3) & 1;
    sprintf(dsp->disasm_str_instr, "cmp #$%06x,%s",
        xxxx, registers_name[d ? DSP_REG_B : DSP_REG_A]);
}

static void dis_cmpu(dsp_core_t* dsp) {
    uint32_t ggg = (dsp->disasm_cur_inst >> 1) & BITMASK(3);
    uint32_t d = dsp->disasm_cur_inst & 1;

    uint32_t srcacc = d ? DSP_REG_B : DSP_REG_A;
    uint32_t srcreg = DSP_REG_NULL;
    switch (ggg) {
    case 0: srcreg = d ? DSP_REG_A : DSP_REG_B; break;
    case 4: srcreg = DSP_REG_X0; break;
    case 5: srcreg = DSP_REG_Y0; break;
    case 6: srcreg = DSP_REG_X1; break;
    case 7: srcreg = DSP_REG_Y1; break;
    }

    sprintf(dsp->disasm_str_instr, "cmpu %s,%s", registers_name[srcreg], registers_name[srcacc]);
}

static void dis_div(dsp_core_t* dsp)
{
    uint32_t srcreg=DSP_REG_NULL, destreg;
    
    switch((dsp->disasm_cur_inst>>4) & BITMASK(2)) {
    case 0: srcreg = DSP_REG_X0; break;
    case 1: srcreg = DSP_REG_Y0; break;
    case 2: srcreg = DSP_REG_X1; break;
    case 3: srcreg = DSP_REG_Y1; break;
    }
    destreg = DSP_REG_A+((dsp->disasm_cur_inst>>3) & 1);

    sprintf(dsp->disasm_str_instr,"div %s,%s", registers_name[srcreg],registers_name[destreg]);
}

static void dis_do_aa(dsp_core_t* dsp)
{
    char name[16];

    dsp->disasm_cur_inst_len++;

    if (dsp->disasm_cur_inst & (1<<6)) {
        sprintf(name, "y:$%04x", (dsp->disasm_cur_inst>>8) & BITMASK(6));
    } else {
        sprintf(name, "x:$%04x", (dsp->disasm_cur_inst>>8) & BITMASK(6));
    }

    sprintf(dsp->disasm_str_instr,"do %s,p:$%04x",
        name,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_do_imm(dsp_core_t* dsp)
{
    dsp->disasm_cur_inst_len++;

    sprintf(dsp->disasm_str_instr,"do #$%04x,p:$%04x",
        ((dsp->disasm_cur_inst>>8) & BITMASK(8))|((dsp->disasm_cur_inst & BITMASK(4))<<8),
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_do_ea(dsp_core_t* dsp)
{
    char addr_name[16], name[16];
    uint32_t ea_mode;
    
    dsp->disasm_cur_inst_len++;

    ea_mode = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    dis_calc_ea(dsp, ea_mode, addr_name);

    if (dsp->disasm_cur_inst & (1<<6)) {
        sprintf(name, "y:%s", addr_name);
    } else {
        sprintf(name, "x:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"do %s,p:$%04x", 
        name,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_do_reg(dsp_core_t* dsp)
{
    dsp->disasm_cur_inst_len++;

    sprintf(dsp->disasm_str_instr,"do %s,p:$%04x",
        registers_name[(dsp->disasm_cur_inst>>8) & BITMASK(6)],
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_dor_imm(dsp_core_t* dsp)
{
    uint32_t addr = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t xxx = ((dsp->disasm_cur_inst>>8) & BITMASK(8)) | ((dsp->disasm_cur_inst & BITMASK(4))<<8);

    sprintf(dsp->disasm_str_instr,"dor #$%04x,p:$%04x",
        xxx, (dsp->pc + addr) & BITMASK(24));
}

static void dis_dor_reg(dsp_core_t* dsp)
{
    uint32_t addr = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t numreg = (dsp->disasm_cur_inst >> 8) & BITMASK(6);

    sprintf(dsp->disasm_str_instr,"dor %s,p:$%04x",
        registers_name[numreg], (dsp->pc + addr) & BITMASK(24));
}

static void dis_jcc_ea(dsp_core_t* dsp)
{
    char cond_name[16], addr_name[16];
    uint32_t cc_code=0;
    
    dis_calc_ea(dsp, (dsp->disasm_cur_inst >>8) & BITMASK(6), addr_name);
    cc_code=dsp->disasm_cur_inst & BITMASK(4);
    dis_calc_cc(dsp, cc_code, cond_name);    

    sprintf(dsp->disasm_str_instr,"j%s p:%s", cond_name, addr_name);
}

static void dis_jcc_imm(dsp_core_t* dsp)
{
    char cond_name[16], addr_name[16];
    uint32_t cc_code=0;
    
    sprintf(addr_name, "$%04x", dsp->disasm_cur_inst & BITMASK(12));
    cc_code=(dsp->disasm_cur_inst>>12) & BITMASK(4);
    dis_calc_cc(dsp, cc_code, cond_name);    

    sprintf(dsp->disasm_str_instr,"j%s p:%s", cond_name, addr_name);
}

static void dis_jclr_aa(dsp_core_t* dsp)
{
    /* jclr #n,x:aa,p:xx */
    /* jclr #n,y:aa,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(srcname, "y:$%04x", value);
    } else {
        sprintf(srcname, "x:$%04x", value);
    }

    sprintf(dsp->disasm_str_instr,"jclr #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jclr_ea(dsp_core_t* dsp)
{
    /* jclr #n,x:ea,p:xx */
    /* jclr #n,y:ea,p:xx */
    char srcname[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(srcname, "y:%s", addr_name);
    } else {
        sprintf(srcname, "x:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"jclr #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jclr_pp(dsp_core_t* dsp)
{
    /* jclr #n,x:pp,p:xx */
    /* jclr #n,y:pp,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    value += 0xffffc0;
    if (memspace) {
        sprintf(srcname, "y:$%06x", value);
    } else {
        sprintf(srcname, "x:$%06x", value);
    }

    sprintf(dsp->disasm_str_instr,"jclr #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jclr_reg(dsp_core_t* dsp)
{
    /* jclr #n,R,p:xx */
    uint32_t value, numbit;
    
    dsp->disasm_cur_inst_len++;

    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"jclr #%d,%s,p:$%04x",
        numbit,
        registers_name[value],
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jmp_imm(dsp_core_t* dsp)
{
    sprintf(dsp->disasm_str_instr,"jmp p:$%04x", dsp->disasm_cur_inst & BITMASK(12));
}

static void dis_jmp_ea(dsp_core_t* dsp)
{
    char dstname[16];

    dis_calc_ea(dsp, (dsp->disasm_cur_inst >>8) & BITMASK(6), dstname);

    sprintf(dsp->disasm_str_instr,"jmp p:%s", dstname);
}

static void dis_jscc_ea(dsp_core_t* dsp)
{
    char cond_name[16], addr_name[16];
    uint32_t cc_code=0;
    
    dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6), addr_name);
    cc_code=dsp->disasm_cur_inst & BITMASK(4);
    dis_calc_cc(dsp, cc_code, cond_name);    

    sprintf(dsp->disasm_str_instr,"js%s p:%s", cond_name, addr_name);
}
    
static void dis_jscc_imm(dsp_core_t* dsp)
{
    char cond_name[16], addr_name[16];
    uint32_t cc_code=0;
    
    sprintf(addr_name, "$%04x", dsp->disasm_cur_inst & BITMASK(12));
    cc_code=(dsp->disasm_cur_inst>>12) & BITMASK(4);
    dis_calc_cc(dsp, cc_code, cond_name);    

    sprintf(dsp->disasm_str_instr,"js%s p:%s", cond_name, addr_name);
}

static void dis_jsclr_aa(dsp_core_t* dsp)
{
    /* jsclr #n,x:aa,p:xx */
    /* jsclr #n,y:aa,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(srcname, "y:$%04x", value);
    } else {
        sprintf(srcname, "x:$%04x", value);
    }

    sprintf(dsp->disasm_str_instr,"jsclr #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsclr_ea(dsp_core_t* dsp)
{
    /* jsclr #n,x:ea,p:xx */
    /* jsclr #n,y:ea,p:xx */
    char srcname[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(srcname, "y:%s", addr_name);
    } else {
        sprintf(srcname, "x:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"jsclr #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsclr_pp(dsp_core_t* dsp)
{
    /* jsclr #n,x:pp,p:xx */
    /* jsclr #n,y:pp,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    value += 0xffffc0;
    if (memspace) {
        sprintf(srcname, "y:$%06x", value);
    } else {
        sprintf(srcname, "x:$%06x", value);
    }

    sprintf(dsp->disasm_str_instr,"jsclr #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsclr_reg(dsp_core_t* dsp)
{
    /* jsclr #n,R,p:xx */
    uint32_t value, numbit;
    
    dsp->disasm_cur_inst_len++;

    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"jsclr #%d,%s,p:$%04x",
        numbit,
        registers_name[value],
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jset_aa(dsp_core_t* dsp)
{
    /* jset #n,x:aa,p:xx */
    /* jset #n,y:aa,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(srcname, "y:$%04x", value);
    } else {
        sprintf(srcname, "x:$%04x", value);
    }

    sprintf(dsp->disasm_str_instr,"jset #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jset_ea(dsp_core_t* dsp)
{
    /* jset #n,x:ea,p:xx */
    /* jset #n,y:ea,p:xx */
    char srcname[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(srcname, "y:%s", addr_name);
    } else {
        sprintf(srcname, "x:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"jset #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jset_pp(dsp_core_t* dsp)
{
    /* jset #n,x:pp,p:xx */
    /* jset #n,y:pp,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    value += 0xffffc0;
    if (memspace) {
        sprintf(srcname, "y:$%06x", value);
    } else {
        sprintf(srcname, "x:$%06x", value);
    }

    sprintf(dsp->disasm_str_instr,"jset #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jset_reg(dsp_core_t* dsp)
{
    /* jset #n,R,p:xx */
    uint32_t value, numbit;
    
    dsp->disasm_cur_inst_len++;

    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"jset #%d,%s,p:$%04x",
        numbit,
        registers_name[value],
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsr_imm(dsp_core_t* dsp)
{
    sprintf(dsp->disasm_str_instr,"jsr p:$%04x", dsp->disasm_cur_inst & BITMASK(12));
}

static void dis_jsr_ea(dsp_core_t* dsp)
{
    char dstname[16];

    dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6),dstname);

    sprintf(dsp->disasm_str_instr,"jsr p:%s", dstname);
}

static void dis_jsset_aa(dsp_core_t* dsp)
{
    /* jsset #n,x:aa,p:xx */
    /* jsset #n,y:aa,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    if (memspace) {
        sprintf(srcname, "y:$%04x", value);
    } else {
        sprintf(srcname, "x:$%04x", value);
    }

    sprintf(dsp->disasm_str_instr,"jsset #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsset_ea(dsp_core_t* dsp)
{
    /* jsset #n,x:ea,p:xx */
    /* jsset #n,y:ea,p:xx */
    char srcname[16], addr_name[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    dis_calc_ea(dsp, value, addr_name);
    if (memspace) {
        sprintf(srcname, "y:%s", addr_name);
    } else {
        sprintf(srcname, "x:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"jsset #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsset_pp(dsp_core_t* dsp)
{
    /* jsset #n,x:pp,p:xx */
    /* jsset #n,y:pp,p:xx */
    char srcname[16];
    uint32_t memspace, value, numbit;
    
    dsp->disasm_cur_inst_len++;

    memspace = (dsp->disasm_cur_inst>>6) & 1;
    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    value += 0xffffc0;
    if (memspace) {
        sprintf(srcname, "y:$%06x", value);
    } else {
        sprintf(srcname, "x:$%06x", value);
    }

    sprintf(dsp->disasm_str_instr,"jsset #%d,%s,p:$%04x",
        numbit,
        srcname,
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_jsset_reg(dsp_core_t* dsp)
{
    /* jsset #n,r,p:xx */
    uint32_t value, numbit;
    
    dsp->disasm_cur_inst_len++;

    value = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numbit = dsp->disasm_cur_inst & BITMASK(5);

    sprintf(dsp->disasm_str_instr,"jsset #%d,%s,p:$%04x",
        numbit,
        registers_name[value],
        read_memory_p(dsp, dsp->pc+1)
    );
}

static void dis_lua(dsp_core_t* dsp)
{
    char addr_name[16];

    dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(5), addr_name);
    uint32_t numreg = dsp->disasm_cur_inst & BITMASK(4);
    
    sprintf(dsp->disasm_str_instr,"lua %s,%s", addr_name, registers_name[numreg]);
}

static void dis_lua_rel(dsp_core_t* dsp)
{
    uint32_t aa = ((dsp->disasm_cur_inst >> 4) & BITMASK(4))
                + (((dsp->disasm_cur_inst >> 11) & BITMASK(3)) << 4);
    uint32_t addrreg = (dsp->disasm_cur_inst >> 8) & BITMASK(3);
    uint32_t dstreg = (dsp->disasm_cur_inst & BITMASK(3));

    int32_t aa_s = (int32_t)dsp_signextend(7, aa);

    if (dsp->disasm_cur_inst & (1<<3)) {
        sprintf(dsp->disasm_str_instr,"lua (r%d + %d),n%d",
            addrreg, aa_s, dstreg);
    } else {
        sprintf(dsp->disasm_str_instr,"lua (r%d + %d),r%d",
            addrreg, aa_s, dstreg);
    }

}

static void dis_movec_reg(dsp_core_t* dsp)
{
    uint32_t numreg1, numreg2;

    /* S1,D2 */
    /* S2,D1 */

    numreg2 = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    numreg1 = dsp->disasm_cur_inst & BITMASK(6);

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write D1 */
        sprintf(dsp->disasm_str_instr,"movec %s,%s", registers_name[numreg2], registers_name[numreg1]);
    } else {
        /* Read S1 */
        sprintf(dsp->disasm_str_instr,"movec %s,%s", registers_name[numreg1], registers_name[numreg2]);
    }
}

static void dis_movec_aa(dsp_core_t* dsp)
{
    const char *spacename;
    char srcname[16],dstname[16];
    uint32_t numreg, addr;

    /* x:aa,D1 */
    /* S1,x:aa */
    /* y:aa,D1 */
    /* S1,y:aa */

    numreg = dsp->disasm_cur_inst & BITMASK(6);
    addr = (dsp->disasm_cur_inst>>8) & BITMASK(6);

    if (dsp->disasm_cur_inst & (1<<6)) {
        spacename="y";
    } else {
        spacename="x";
    }

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write D1 */
        sprintf(srcname, "%s:$%04x", spacename, addr);
        strcpy(dstname, registers_name[numreg]);
    } else {
        /* Read S1 */
        strcpy(srcname, registers_name[numreg]);
        sprintf(dstname, "%s:$%04x", spacename, addr);
    }

    sprintf(dsp->disasm_str_instr,"movec %s,%s", srcname, dstname);
}

static void dis_movec_imm(dsp_core_t* dsp)
{
    uint32_t numreg;

    /* #xx,D1 */

    numreg = dsp->disasm_cur_inst & BITMASK(6);

    sprintf(dsp->disasm_str_instr,"movec #$%02x,%s", (dsp->disasm_cur_inst>>8) & BITMASK(8), registers_name[numreg]);
}

static void dis_movec_ea(dsp_core_t* dsp)
{
    const char *spacename;
    char srcname[16], dstname[16], addr_name[16];
    uint32_t numreg, ea_mode;
    int retour;

    /* x:ea,D1 */
    /* S1,x:ea */
    /* y:ea,D1 */
    /* S1,y:ea */
    /* #xxxx,D1 */

    numreg = dsp->disasm_cur_inst & BITMASK(6);
    ea_mode = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    retour = dis_calc_ea(dsp, ea_mode, addr_name);

    if (dsp->disasm_cur_inst & (1<<6)) {
        spacename="y";
    } else {
        spacename="x";
    }

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write D1 */
        if (retour) {
            sprintf(srcname, "#%s", addr_name);
        } else {
            sprintf(srcname, "%s:%s", spacename, addr_name);
        }
        strcpy(dstname, registers_name[numreg]);
    } else {
        /* Read S1 */
        strcpy(srcname, registers_name[numreg]);
        sprintf(dstname, "%s:%s", spacename, addr_name);
    }

    sprintf(dsp->disasm_str_instr,"movec %s,%s", srcname, dstname);
}

static void dis_movem_aa(dsp_core_t* dsp)
{
    /* S,p:aa */
    /* p:aa,D */
    char addr_name[16], srcname[16], dstname[16];
    uint32_t numreg;

    sprintf(addr_name, "$%04x",(dsp->disasm_cur_inst>>8) & BITMASK(6));
    numreg = dsp->disasm_cur_inst & BITMASK(6);
    if  (dsp->disasm_cur_inst & (1<<15)) {
        /* Write D */
        sprintf(srcname, "p:%s", addr_name);
        strcpy(dstname, registers_name[numreg]);
    } else {
        /* Read S */
        strcpy(srcname, registers_name[numreg]);
        sprintf(dstname, "p:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"movem %s,%s", srcname, dstname);
}

static void dis_movem_ea(dsp_core_t* dsp)
{
    /* S,p:ea */
    /* p:ea,D */
    char addr_name[16], srcname[16], dstname[16];
    uint32_t ea_mode, numreg;

    ea_mode = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    dis_calc_ea(dsp, ea_mode, addr_name);
    numreg = dsp->disasm_cur_inst & BITMASK(6);
    if  (dsp->disasm_cur_inst & (1<<15)) {
        /* Write D */
        sprintf(srcname, "p:%s", addr_name);
        strcpy(dstname, registers_name[numreg]);
    } else {
        /* Read S */
        strcpy(srcname, registers_name[numreg]);
        sprintf(dstname, "p:%s", addr_name);
    }

    sprintf(dsp->disasm_str_instr,"movem %s,%s", srcname, dstname);
}

static void dis_movep_0(dsp_core_t* dsp)
{
    char srcname[16]="",dstname[16]="";
    uint32_t addr, memspace, numreg;

    /* S,x:pp */
    /* x:pp,D */
    /* S,y:pp */
    /* y:pp,D */

    addr = 0xffffc0 + (dsp->disasm_cur_inst & BITMASK(6));
    memspace = (dsp->disasm_cur_inst>>16) & 1;
    numreg = (dsp->disasm_cur_inst>>8) & BITMASK(6);

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write pp */

        strcpy(srcname, registers_name[numreg]);

        if (memspace) {
            sprintf(dstname, "y:$%06x", addr);
        } else {
            sprintf(dstname, "x:$%06x", addr);
        }
    } else {
        /* Read pp */

        if (memspace) {
            sprintf(srcname, "y:$%06x", addr);
        } else {
            sprintf(srcname, "x:$%06x", addr);
        }

        strcpy(dstname, registers_name[numreg]);
    }

    sprintf(dsp->disasm_str_instr,"movep %s,%s", srcname, dstname);
}

static void dis_movep_1(dsp_core_t* dsp)
{
    char srcname[16]="",dstname[16]="",name[16]="";
    uint32_t addr, memspace; 

    /* p:ea,x:pp */
    /* x:pp,p:ea */
    /* p:ea,y:pp */
    /* y:pp,p:ea */

    addr = 0xffffc0 + (dsp->disasm_cur_inst & BITMASK(6));
    dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6), name);
    memspace = (dsp->disasm_cur_inst>>16) & 1;

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write pp */

        sprintf(srcname, "p:%s", name);

        if (memspace) {
            sprintf(dstname, "y:$%06x", addr);
        } else {
            sprintf(dstname, "x:$%06x", addr);
        }
    } else {
        /* Read pp */

        if (memspace) {
            sprintf(srcname, "y:$%06x", addr);
        } else {
            sprintf(srcname, "x:$%06x", addr);
        }

        sprintf(dstname, "p:%s", name);
    }

    sprintf(dsp->disasm_str_instr,"movep %s,%s", srcname, dstname);
}

static void dis_movep_23(dsp_core_t* dsp)
{
    char srcname[16]="",dstname[16]="",name[16]="";
    uint32_t addr, memspace, easpace, retour; 

    /* x:ea,x:pp */
    /* y:ea,x:pp */
    /* #xxxxxx,x:pp */
    /* x:pp,x:ea */
    /* x:pp,y:ea */

    /* x:ea,y:pp */
    /* y:ea,y:pp */
    /* #xxxxxx,y:pp */
    /* y:pp,y:ea */
    /* y:pp,x:ea */

    addr = 0xffffc0 + (dsp->disasm_cur_inst & BITMASK(6));
    retour = dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6), name);
    memspace = (dsp->disasm_cur_inst>>16) & 1;
    easpace = (dsp->disasm_cur_inst>>6) & 1;

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write pp */

        if (retour) {
            sprintf(srcname, "#%s", name);
        } else {
            if (easpace) {
                sprintf(srcname, "y:%s", name);
            } else {
                sprintf(srcname, "x:%s", name);
            }
        }

        if (memspace) {
            sprintf(dstname, "y:$%06x", addr);
        } else {
            sprintf(dstname, "x:$%06x", addr);
        }
    } else {
        /* Read pp */

        if (memspace) {
            sprintf(srcname, "y:$%06x", addr);
        } else {
            sprintf(srcname, "x:$%06x", addr);
        }

        if (easpace) {
            sprintf(dstname, "y:%s", name);
        } else {
            sprintf(dstname, "x:%s", name);
        }
    }

    sprintf(dsp->disasm_str_instr,"movep %s,%s", srcname, dstname);
}

static void dis_movep_x_qq(dsp_core_t* dsp) {
    // 00000111W1MMMRRR0Sqqqqqq

    char srcname[16]="",dstname[16]="",name[16]="";

    uint32_t addr = 0xffff80 + (dsp->disasm_cur_inst & BITMASK(6));
    uint32_t ea_mode = (dsp->disasm_cur_inst>>8) & BITMASK(6);
    uint32_t easpace = (dsp->disasm_cur_inst>>6) & 1;
    int retour = dis_calc_ea(dsp, ea_mode, name);

    if (dsp->disasm_cur_inst & (1<<15)) {
        /* Write qq */

        if (retour) {
            sprintf(srcname, "#%s", name);
        } else {
            if (easpace) {
                sprintf(srcname, "y:%s", name);
            } else {
                sprintf(srcname, "x:%s", name);
            }
        }

        sprintf(dstname, "x:$%04x", addr);
    } else {
        /* Read qq */

        sprintf(srcname, "x:$%04x", addr);

        if (easpace) {
            sprintf(dstname, "y:%s", name);
        } else {
            sprintf(dstname, "x:%s", name);
        }
    }

    sprintf(dsp->disasm_str_instr,"movep %s,%s", srcname, dstname);
}


static void dis_move_x_long(dsp_core_t* dsp) {
    dsp->disasm_cur_inst_len++;

    int W = (dsp->disasm_cur_inst >> 6) & 1;
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    uint32_t offreg = DSP_REG_R0 + ((dsp->disasm_cur_inst >> 8) & BITMASK(3));
    uint32_t numreg = dsp->disasm_cur_inst & BITMASK(6);

    int32_t xxxx_s = dsp_signextend(24, xxxx);
    if (W) {
        sprintf(dsp->disasm_str_instr, "move x:(%s + %d), %s",
            registers_name[offreg], xxxx_s, registers_name[numreg]);
    } else {
        sprintf(dsp->disasm_str_instr, "move %s, x:(%s + %d)",
            registers_name[numreg], registers_name[offreg], xxxx_s);
    }
}

static void dis_move_xy_imm(dsp_core_t* dsp, int space)
{
    char space_c = space == DSP_SPACE_X ? 'x' : 'y';

    int W = (dsp->disasm_cur_inst >> 4) & 1;
    uint32_t xxx = (((dsp->disasm_cur_inst >> 11) & BITMASK(6)) << 1)
             + ((dsp->disasm_cur_inst >> 6) & 1);
    uint32_t offreg = DSP_REG_R0 + ((dsp->disasm_cur_inst >> 8) & BITMASK(3));
    uint32_t numreg = dsp->disasm_cur_inst & BITMASK(4);
   
    int32_t xxx_s = dsp_signextend(7, xxx);
    if (W) {
        sprintf(dsp->disasm_str_instr, "move %c:(%s + %d), %s",
            space_c, registers_name[offreg], xxx_s, registers_name[numreg]);
    } else {
        sprintf(dsp->disasm_str_instr, "move %s, %c:(%s + %d)",
            registers_name[numreg], space_c, registers_name[offreg], xxx_s);
    }
}

static void dis_move_x_imm(dsp_core_t* dsp)
{
    dis_move_xy_imm(dsp, DSP_SPACE_X);
}

static void dis_move_y_imm(dsp_core_t* dsp) {
    dis_move_xy_imm(dsp, DSP_SPACE_Y);
}

static void dis_mpyi(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;

    uint32_t k = (dsp->disasm_cur_inst >> 2) & 1;
    uint32_t d = (dsp->disasm_cur_inst >> 3) & 1;
    uint32_t qq = (dsp->disasm_cur_inst >> 4) & BITMASK(2);

    unsigned int srcreg = DSP_REG_NULL;
    switch (qq) {
    case 0: srcreg = DSP_REG_X0; break;
    case 1: srcreg = DSP_REG_Y0; break;
    case 2: srcreg = DSP_REG_X1; break;
    case 3: srcreg = DSP_REG_Y1; break;
    }

    unsigned int destreg = d ? DSP_REG_B : DSP_REG_A;

    sprintf(dsp->disasm_str_instr, "mpyi %s#$%06x,%s,%s",
        k ? "-" : "+", xxxx,
        registers_name[srcreg], registers_name[destreg]);
}

static void dis_norm(dsp_core_t* dsp)
{
    uint32_t srcreg, destreg;

    srcreg = DSP_REG_R0+((dsp->disasm_cur_inst>>8) & BITMASK(3));
    destreg = DSP_REG_A+((dsp->disasm_cur_inst>>3) & 1);

    sprintf(dsp->disasm_str_instr,"norm %s,%s", registers_name[srcreg], registers_name[destreg]);
}

static void dis_or_long(dsp_core_t* dsp)
{
    dsp->disasm_cur_inst_len++;
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    uint32_t accname = ((dsp->disasm_cur_inst >> 3) & 1) ? DSP_REG_B : DSP_REG_A;
    sprintf(dsp->disasm_str_instr, "or #$%04x,%s", xxxx, registers_name[accname]);
}

static void dis_ori(dsp_core_t* dsp)
{
    switch(dsp->disasm_cur_inst & BITMASK(2)) {
        case 0:
            sprintf(dsp->disasm_str_instr,"ori #$%02x,mr", (dsp->disasm_cur_inst>>8) & BITMASK(8));
            break;
        case 1:
            sprintf(dsp->disasm_str_instr,"ori #$%02x,ccr", (dsp->disasm_cur_inst>>8) & BITMASK(8));
            break;
        case 2:
            sprintf(dsp->disasm_str_instr,"ori #$%02x,omr", (dsp->disasm_cur_inst>>8) & BITMASK(8));
            break;
        default:
            break;
    }

}

static void dis_rep_aa(dsp_core_t* dsp)
{
    char name[16];

    /* x:aa */
    /* y:aa */

    if (dsp->disasm_cur_inst & (1<<6)) {
        sprintf(name, "y:$%04x",(dsp->disasm_cur_inst>>8) & BITMASK(6));
    } else {
        sprintf(name, "x:$%04x",(dsp->disasm_cur_inst>>8) & BITMASK(6));
    }

    sprintf(dsp->disasm_str_instr,"rep %s", name);
}

static void dis_rep_imm(dsp_core_t* dsp)
{
    /* #xxx */
    sprintf(dsp->disasm_str_instr,"rep #$%02x", ((dsp->disasm_cur_inst>>8) & BITMASK(8))
        + ((dsp->disasm_cur_inst & BITMASK(4))<<8));
}

static void dis_rep_ea(dsp_core_t* dsp)
{
    char name[16],addr_name[16];

    /* x:ea */
    /* y:ea */

    dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6), addr_name);
    if (dsp->disasm_cur_inst & (1<<6)) {
        sprintf(name, "y:%s",addr_name);
    } else {
        sprintf(name, "x:%s",addr_name);
    }

    sprintf(dsp->disasm_str_instr,"rep %s", name);
}

static void dis_rep_reg(dsp_core_t* dsp)
{
    /* R */

    sprintf(dsp->disasm_str_instr,"rep %s", registers_name[(dsp->disasm_cur_inst>>8) & BITMASK(6)]);
}

static void dis_sub_imm(dsp_core_t* dsp)
{
    uint32_t xx = (dsp->disasm_cur_inst >> 8) & BITMASK(6);
    uint32_t d = (dsp->disasm_cur_inst >> 3) & 1;
    sprintf(dsp->disasm_str_instr, "sub #$%02x,%s",
        xx, registers_name[d ? DSP_REG_B : DSP_REG_A]);
}

static void dis_sub_long(dsp_core_t* dsp)
{
    uint32_t xxxx = read_memory_p(dsp, dsp->pc+1);
    dsp->disasm_cur_inst_len++;
    uint32_t d = (dsp->disasm_cur_inst >> 3) & 1;
    sprintf(dsp->disasm_str_instr, "sub #$%06x,%s",
        xxxx, registers_name[d ? DSP_REG_B : DSP_REG_A]);
}

static void dis_tcc(dsp_core_t* dsp)
{
    char ccname[16];
    uint32_t src1reg, dst1reg, src2reg, dst2reg;

    dis_calc_cc(dsp, (dsp->disasm_cur_inst>>12) & BITMASK(4), ccname);
    src1reg = registers_tcc[(dsp->disasm_cur_inst>>3) & BITMASK(4)][0];
    dst1reg = registers_tcc[(dsp->disasm_cur_inst>>3) & BITMASK(4)][1];

    if (dsp->disasm_cur_inst & (1<<16)) {
        src2reg = DSP_REG_R0+((dsp->disasm_cur_inst>>8) & BITMASK(3));
        dst2reg = DSP_REG_R0+(dsp->disasm_cur_inst & BITMASK(3));

        sprintf(dsp->disasm_str_instr,"t%s %s,%s %s,%s",
            ccname,
            registers_name[src1reg],
            registers_name[dst1reg],
            registers_name[src2reg],
            registers_name[dst2reg]
        );
    } else {
        sprintf(dsp->disasm_str_instr,"t%s %s,%s",
            ccname,
            registers_name[src1reg],
            registers_name[dst1reg]
        );
    }
}


/**********************************
 *  Parallel moves
 **********************************/

static void dis_pm(dsp_core_t* dsp)
{
    uint32_t value = (dsp->disasm_cur_inst >> 20) & BITMASK(4);
    disasm_opcodes_parmove[value](dsp);
}

static void dis_pm_0(dsp_core_t* dsp)
{
    char space_name[16], addr_name[16];
    uint32_t memspace, numreg1, numreg2;
/*
    0000 100d 00mm mrrr S,x:ea  x0,D
    0000 100d 10mm mrrr S,y:ea  y0,D
*/
    memspace = (dsp->disasm_cur_inst>>15) & 1;
    numreg1 = DSP_REG_A+((dsp->disasm_cur_inst>>16) & 1);
    dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6), addr_name);

    if (memspace) {
        strcpy(space_name,"y");
        numreg2 = DSP_REG_Y0;
    } else {
        strcpy(space_name,"x");
        numreg2 = DSP_REG_X0;
    }

    sprintf(dsp->disasm_parallelmove_name,
        "%s,%s:%s %s,%s",
        registers_name[numreg1],
        space_name,
        addr_name,
        registers_name[numreg2],
        registers_name[numreg1]
    );
}

static void dis_pm_1(dsp_core_t* dsp)
{
/*
    0001 ffdf w0mm mrrr x:ea,D1     S2,D2
                        S1,x:ea     S2,D2
                        #xxxxxx,D1  S2,D2
    0001 deff w1mm mrrr S1,D1       y:ea,D2
                        S1,D1       S2,y:ea
                        S1,D1       #xxxxxx,D2
*/

    char addr_name[16];
    uint32_t memspace, write_flag, retour, s1reg, s2reg, d1reg, d2reg;

    memspace = (dsp->disasm_cur_inst>>14) & 1;
    write_flag = (dsp->disasm_cur_inst>>15) & 1;
    retour = dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(6), addr_name);

    if (memspace==DSP_SPACE_Y) {
        s2reg = d2reg = DSP_REG_Y0;
        switch((dsp->disasm_cur_inst>>16) & BITMASK(2)) {
            case 0: s2reg = d2reg = DSP_REG_Y0; break;
            case 1: s2reg = d2reg = DSP_REG_Y1; break;
            case 2: s2reg = d2reg = DSP_REG_A;  break;
            case 3: s2reg = d2reg = DSP_REG_B;  break;
        }

        s1reg = DSP_REG_A+((dsp->disasm_cur_inst>>19) & 1);
        d1reg = DSP_REG_X0+((dsp->disasm_cur_inst>>18) & 1);

        if (write_flag) {
            /* Write D2 */

            if (retour) {
                sprintf(dsp->disasm_parallelmove_name,"%s,%s #%s,%s",
                    registers_name[s1reg],
                    registers_name[d1reg],
                    addr_name,
                    registers_name[d2reg]
                );
            } else {
                sprintf(dsp->disasm_parallelmove_name,"%s,%s y:%s,%s",
                    registers_name[s1reg],
                    registers_name[d1reg],
                    addr_name,
                    registers_name[d2reg]
                );
            }
        } else {
            /* Read S2 */
            sprintf(dsp->disasm_parallelmove_name,"%s,%s %s,y:%s",
                registers_name[s1reg],
                registers_name[d1reg],
                registers_name[s2reg],
                addr_name
            );
        }       

    } else {
        s1reg = d1reg = DSP_REG_X0;
        switch((dsp->disasm_cur_inst>>18) & BITMASK(2)) {
            case 0: s1reg = d1reg = DSP_REG_X0; break;
            case 1: s1reg = d1reg = DSP_REG_X1; break;
            case 2: s1reg = d1reg = DSP_REG_A;  break;
            case 3: s1reg = d1reg = DSP_REG_B;  break;
        }

        s2reg = DSP_REG_A+((dsp->disasm_cur_inst>>17) & 1);
        d2reg = DSP_REG_Y0+((dsp->disasm_cur_inst>>16) & 1);

        if (write_flag) {
            /* Write D1 */

            if (retour) {
                sprintf(dsp->disasm_parallelmove_name,"#%s,%s %s,%s",
                    addr_name,
                    registers_name[d1reg],
                    registers_name[s2reg],
                    registers_name[d2reg]
                );
            } else {
                sprintf(dsp->disasm_parallelmove_name,"x:%s,%s %s,%s",
                    addr_name,
                    registers_name[d1reg],
                    registers_name[s2reg],
                    registers_name[d2reg]
                );
            }
        } else {
            /* Read S1 */
            sprintf(dsp->disasm_parallelmove_name,"%s,x:%s %s,%s",
                registers_name[s1reg],
                addr_name,
                registers_name[s2reg],
                registers_name[d2reg]
            );
        }       
    
    }
}

static void dis_pm_2(dsp_core_t* dsp)
{
    char addr_name[16];
    uint32_t numreg1, numreg2;
/*
    0010 0000 0000 0000 nop
    0010 0000 010m mrrr R update
    0010 00ee eeed dddd S,D
    001d dddd iiii iiii #xx,D
*/
    if (((dsp->disasm_cur_inst >> 8) & 0xffff) == 0x2000) {
        return;
    }

    if (((dsp->disasm_cur_inst >> 8) & 0xffe0) == 0x2040) {
        dis_calc_ea(dsp, (dsp->disasm_cur_inst>>8) & BITMASK(5), addr_name);
        sprintf(dsp->disasm_parallelmove_name, "%s,r%d",addr_name, (dsp->disasm_cur_inst>>8) & BITMASK(3));
        return;
    }

    if (((dsp->disasm_cur_inst >> 8) & 0xfc00) == 0x2000) {
        numreg1 = (dsp->disasm_cur_inst>>13) & BITMASK(5);
        numreg2 = (dsp->disasm_cur_inst>>8) & BITMASK(5);
        sprintf(dsp->disasm_parallelmove_name, "%s,%s", registers_name[numreg1], registers_name[numreg2]); 
        return;
    }

    numreg1 = (dsp->disasm_cur_inst>>16) & BITMASK(5);
    sprintf(dsp->disasm_parallelmove_name, "#$%02x,%s", (dsp->disasm_cur_inst >> 8) & BITMASK(8), registers_name[numreg1]);
}

static void dis_pm_4(dsp_core_t* dsp)
{
    char addr_name[16];
    uint32_t value, retour, ea_mode, memspace;
/*
    0100 l0ll w0aa aaaa l:aa,D
                        S,l:aa
    0100 l0ll w1mm mrrr l:ea,D
                        S,l:ea
    01dd 0ddd w0aa aaaa x:aa,D
                        S,x:aa
    01dd 0ddd w1mm mrrr x:ea,D
                        S,x:ea
                        #xxxxxx,D
    01dd 1ddd w0aa aaaa y:aa,D
                        S,y:aa
    01dd 1ddd w1mm mrrr y:ea,D
                        S,y:ea
                        #xxxxxx,D
*/
    value = (dsp->disasm_cur_inst>>16) & BITMASK(3);
    value |= (dsp->disasm_cur_inst>>17) & (BITMASK(2)<<3);

    ea_mode = (dsp->disasm_cur_inst>>8) & BITMASK(6);

    if ((value>>2)==0) {
        /* L: memory move */
        if (dsp->disasm_cur_inst & (1<<14)) {
            retour = dis_calc_ea(dsp, ea_mode, addr_name);   
        } else {
            sprintf(addr_name,"$%04x", ea_mode);
            retour = 0;
        }

        value = (dsp->disasm_cur_inst>>16) & BITMASK(2);
        value |= (dsp->disasm_cur_inst>>17) & (1<<2);

        if (dsp->disasm_cur_inst & (1<<15)) {
            /* Write D */

            if (retour) {
                sprintf(dsp->disasm_parallelmove_name, "#%s,%s", addr_name, registers_lmove[value]);
            } else {
                sprintf(dsp->disasm_parallelmove_name, "l:%s,%s", addr_name, registers_lmove[value]);
            }
        } else {
            /* Read S */
            sprintf(dsp->disasm_parallelmove_name, "%s,l:%s", registers_lmove[value], addr_name);
        }

        return;
    }

    memspace = (dsp->disasm_cur_inst>>19) & 1;
    if (dsp->disasm_cur_inst & (1<<14)) {
        retour = dis_calc_ea(dsp, ea_mode, addr_name);   
    } else {
        sprintf(addr_name,"$%04x", ea_mode);
        retour = 0;
    }

    if (memspace) {
        /* Y: */

        if (dsp->disasm_cur_inst & (1<<15)) {
            /* Write D */

            if (retour) {
                sprintf(dsp->disasm_parallelmove_name, "#%s,%s", addr_name, registers_name[value]);
            } else {
                sprintf(dsp->disasm_parallelmove_name, "y:%s,%s", addr_name, registers_name[value]);
            }

        } else {
            /* Read S */
            sprintf(dsp->disasm_parallelmove_name, "%s,y:%s", registers_name[value], addr_name);
        }
    } else {
        /* X: */

        if (dsp->disasm_cur_inst & (1<<15)) {
            /* Write D */

            if (retour) {
                sprintf(dsp->disasm_parallelmove_name, "#%s,%s", addr_name, registers_name[value]);
            } else {
                sprintf(dsp->disasm_parallelmove_name, "x:%s,%s", addr_name, registers_name[value]);
            }
        } else {
            /* Read S */
            sprintf(dsp->disasm_parallelmove_name, "%s,x:%s", registers_name[value], addr_name);
        }
    }
}

static void dis_pm_8(dsp_core_t* dsp)
{
    char addr1_name[16], addr2_name[16];
    uint32_t ea_mode1, ea_mode2, numreg1, numreg2;
/*
    1wmm eeff WrrM MRRR x:ea,D1     y:ea,D2 
                        x:ea,D1     S2,y:ea
                        S1,x:ea     y:ea,D2
                        S1,x:ea     S2,y:ea
*/
    numreg1 = DSP_REG_X0;
    switch((dsp->disasm_cur_inst>>18) & BITMASK(2)) {
        case 0: numreg1 = DSP_REG_X0;   break;
        case 1: numreg1 = DSP_REG_X1;   break;
        case 2: numreg1 = DSP_REG_A;    break;
        case 3: numreg1 = DSP_REG_B;    break;
    }

    numreg2 = DSP_REG_Y0;
    switch((dsp->disasm_cur_inst>>16) & BITMASK(2)) {
        case 0: numreg2 = DSP_REG_Y0;   break;
        case 1: numreg2 = DSP_REG_Y1;   break;
        case 2: numreg2 = DSP_REG_A;    break;
        case 3: numreg2 = DSP_REG_B;    break;
    }

    ea_mode1 = (dsp->disasm_cur_inst>>8) & BITMASK(5);
    if ((ea_mode1>>3) == 0) {
        ea_mode1 |= (1<<5);
    }
    ea_mode2 = (dsp->disasm_cur_inst>>13) & BITMASK(2);
    ea_mode2 |= ((dsp->disasm_cur_inst>>20) & BITMASK(2))<<3;
    if ((ea_mode1 & (1<<2))==0) {
        ea_mode2 |= 1<<2;
    }
    if ((ea_mode2>>3) == 0) {
        ea_mode2 |= (1<<5);
    }

    dis_calc_ea(dsp, ea_mode1, addr1_name);
    dis_calc_ea(dsp, ea_mode2, addr2_name);
    
    if (dsp->disasm_cur_inst & (1<<15)) {
        if (dsp->disasm_cur_inst & (1<<22)) {
            sprintf(dsp->disasm_parallelmove_name, "x:%s,%s y:%s,%s",
                addr1_name,
                registers_name[numreg1],
                addr2_name,
                registers_name[numreg2]
            );
        } else {
            sprintf(dsp->disasm_parallelmove_name, "x:%s,%s %s,y:%s",
                addr1_name,
                registers_name[numreg1],
                registers_name[numreg2],
                addr2_name
            );
        }
    } else {
        if (dsp->disasm_cur_inst & (1<<22)) {
            sprintf(dsp->disasm_parallelmove_name, "%s,x:%s y:%s,%s",
                registers_name[numreg1],
                addr1_name,
                addr2_name,
                registers_name[numreg2]
            );
        } else {
            sprintf(dsp->disasm_parallelmove_name, "%s,x:%s %s,y:%s",
                registers_name[numreg1],
                addr1_name,
                registers_name[numreg2],
                addr2_name
            );
        }
    }   
}
