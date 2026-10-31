/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Run every intrinsic of hlx_asm.h on inputs that differ in each lane, and
 * compare a checksum of the result with the value from the reference
 * simulator (hexagon-sim -mv85qa_1).  The inputs cover both signs, the
 * saturation limits, and shift amounts on both sides of zero.
 */

#include "hlx_test.h"

static HLX_Vector va, vb, vc, vd;
static HLX_VectorPair pa, pb, pc;

/* Values that hit the corner cases, mixed with a pseudo-random sequence */
static const uint16_t corners[] = {
    0x0000, 0x0001, 0x7fff, 0x8000, 0x8001, 0xffff, 0xfffe, 0x00ff,
    0xff00, 0x0010, 0xfff0, 0x0021, 0xffdf, 0x4000, 0xc000, 0x1234,
};

static void fill(HLX_Vector *v, uint32_t seed)
{
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        seed = seed * 1103515245u + 12345u;
        v->h[i] = (i % 3 == 0) ? corners[(i / 3 + seed) % 16]
                               : (uint16_t)(seed >> 16);
    }
}

/* Small signed values, for operands that are shift amounts */
static void fill_shamt(HLX_Vector *v, uint32_t seed)
{
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        seed = seed * 1103515245u + 12345u;
        v->h[i] = (uint16_t)((int)((seed >> 16) % 41) - 20);
    }
}

static void init_inputs(void)
{
    fill(&va, 1);
    fill(&vb, 2);
    fill_shamt(&vc, 3);
    fill(&vd, 10);
    fill(&pa.lo, 4);
    fill(&pa.hi, 5);
    fill(&pb.lo, 6);
    fill(&pb.hi, 7);
    fill_shamt(&pc.lo, 8);
    fill_shamt(&pc.hi, 9);
}

/* FNV-1a over the result; the lane index is part of the hash */
static uint32_t hash_v(uint32_t h, const HLX_Vector *v)
{
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        h = (h ^ v->h[i]) * 16777619u;
    }
    return h;
}

static uint32_t hash(const void *p, size_t size)
{
    const HLX_Vector *v = p;
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < size / sizeof(HLX_Vector); i++) {
        h = hash_v(h, &v[i]);
    }
    return h;
}

#define RUN(EXPR) \
    do { \
        __typeof__(EXPR) r_ = (EXPR); \
        record(#EXPR, hash(&r_, sizeof(r_))); \
    } while (0)

static const struct {
    const char *name;
    uint32_t hash;
} refs[] = {
    { "Q6_Luh_abs_Lh(va)", 0xe208ed39 },
    { "Q6_Lh_abs_Lh_sat(va)", 0xe208ed39 },
    { "Q6_LWuh_abs_LWh(pa)", 0x74782dca },
    { "Q6_LWh_abs_LWh_sat(pa)", 0x74782dca },
    { "Q6_LWuw_abs_LWw(pa)", 0xdee4d30d },
    { "Q6_LWw_abs_LWw_sat(pa)", 0xdee4d30d },
    { "Q6_Luh_absdiff_LhLh(va, vb)", 0x9ee5549c },
    { "Q6_Luh_absdiff_LuhLuh(va, vb)", 0x862d481c },
    { "Q6_LWuw_absdiff_LWuwLWuw(pa, pb)", 0xba0386ea },
    { "Q6_LWuw_absdiff_LWwLWw(pa, pb)", 0x2ae4bcdd },
    { "Q6_Lh_add_LhLh(va, vb)", 0xd4fc73ee },
    { "Q6_Lh_add_LhLh_sat(va, vb)", 0x89a0022a },
    { "Q6_LWh_add_LWhLWh(pa, pb)", 0x42d44fc6 },
    { "Q6_LWh_add_LWhLWh_sat(pa, pb)", 0x92e5270d },
    { "Q6_Luh_add_LuhLuh_sat(va, vb)", 0x7bc2dc15 },
    { "Q6_Luh_add_LhLh_sat(va, vb)", 0x99c92690 },
    { "Q6_LWuh_add_LWhLWh_sat(pa, pb)", 0x4c51008f },
    { "Q6_LWuh_add_LWuhLWuh_sat(pa, pb)", 0x1e245da0 },
    { "Q6_LWuw_add_LWuwLWuw_sat(pa, pb)", 0x02895b4e },
    { "Q6_LWuw_add_LWwLWw_sat(pa, pb)", 0x02700fbe },
    { "Q6_LWw_add_LWwLWw(pa, pb)", 0x6b8dae20 },
    { "Q6_LWw_add_LWwLWw_sat(pa, pb)", 0xbe188e99 },
    { "Q6_LWw_add_LuhLuh(va, vb)", 0xfb54d181 },
    { "Q6_LWw_add_LWwLh(pa, va)", 0xe1a91d98 },
    { "Q6_LWw_add_LWwLuh(pa, va)", 0x95d9e358 },
    { "Q6_L_and_LL(va, vb)", 0x6c4a260f },
    { "Q6_LW_and_LWLW(pa, pb)", 0xe7b38739 },
    { "Q6_L_andn_LL(va, vb)", 0xef59c6e5 },
    { "Q6_LW_andn_LWLW(pa, pb)", 0x3845ed10 },
    { "Q6_Lh_equals_Lh(va)", 0xa7cbb137 },
    { "Q6_Lh_equals_Rh(0x8003fff7u)", 0xf513a9c5 },
    { "Q6_LWw_equals_Rw(0x8003fff7u)", 0x78229dc5 },
    { "Q6_Lh_avg_LhLh(va, vb)", 0x35b56e25 },
    { "Q6_Lh_avg_LhLh_rnd(va, vb)", 0xb5c0023a },
    { "Q6_Luh_avg_LuhLuh(va, vb)", 0x688f6e25 },
    { "Q6_Luh_avg_LuhLuh_rnd(va, vb)", 0x91f7023a },
    { "Q6_LWuw_avg_LWuwLWuw(pa, pb)", 0xffaf0ec2 },
    { "Q6_LWuw_avg_LWuwLWuw_rnd(pa, pb)", 0xb711deea },
    { "Q6_LWw_avg_LWwLWw(pa, pb)", 0x5bfc8ec2 },
    { "Q6_LWw_avg_LWwLWw_rnd(pa, pb)", 0x5bb25eea },
    { "Q6_Lh_cmp_gt_LhLh(va, vb)", 0x298abd92 },
    { "Q6_LWh_cmp_gt_LWhLWh(pa, pb)", 0xba856439 },
    { "Q6_Lh_cmp_gt_LuhLuh(va, vb)", 0x5ed1611c },
    { "Q6_LWh_cmp_gt_LWuhLWuh(pa, pb)", 0x6721933f },
    { "Q6_LWw_cmp_gt_LWuwLWuw(pa, pb)", 0xa22f6dc5 },
    { "Q6_LWw_cmp_gt_LWwLWw(pa, pb)", 0x072b56b1 },
    { "Q6_Lb_packhi_LhLh(va, vb)", 0x64b5261a },
    { "Q6_Lb_packlo_LhLh(va, vb)", 0x63f3c4c8 },
    { "Q6_LWh_unpackhi_Lb(va)", 0x24e6aec5 },
    { "Q6_LWh_unpacklo_Lb(va)", 0x2416b6b0 },
    { "Q6_LWuw_zxt_Luh(va)", 0x314ca137 },
    { "Q6_LWw_sxt_Lh(va)", 0x6fa3a531 },
    { "Q6_Lh_lut4_LuhPh(va, 0x8001400220037ffcull)", 0x85ece3b4 },
    { "Q6_Lh_lut4_LuhPh_mask(va, 0x8001400220037ffcull)", 0x85eabb7f },
    { "Q6_LWw_lut4_LuhPuh_rs1(va, 0x8001400220037ffcull)", 0xc6c71e5a },
    { "Q6_LWw_lut4_LuhPuh_rs1_neg(va, 0x8001400220037ffcull)", 0x967a1e5a },
    { "Q6_Lh_max_LhLh(va, vb)", 0xa2280b7f },
    { "Q6_LWh_max_LWhLWh(pa, pb)", 0x54976798 },
    { "Q6_Luh_max_LuhLuh(va, vb)", 0x10e49b8d },
    { "Q6_LWuh_max_LWuhLWuh(pa, pb)", 0x7beb3ada },
    { "Q6_LWuw_max_LWuwLWuw(pa, pb)", 0x22b0216e },
    { "Q6_LWw_max_LWwLWw(pa, pb)", 0x9db97202 },
    { "Q6_Lh_min_LhLh(va, vb)", 0xb9bbad5c },
    { "Q6_LWh_min_LWhLWh(pa, pb)", 0x3f5c87ef },
    { "Q6_Luh_min_LuhLuh(va, vb)", 0x88885262 },
    { "Q6_LWuh_min_LWuhLWuh(pa, pb)", 0x19535359 },
    { "Q6_LWuw_min_LWuwLWuw(pa, pb)", 0xa9cc9df5 },
    { "Q6_LWw_min_LWwLWw(pa, pb)", 0x2d4e5035 },
    { "Q6_LWuw_add_mpy_hi_LuhLWuhLWw(va, pa, pb)", 0xb10a0e04 },
    { "Q6_LWw_mpyacc_LhLh(pa, va, vb)", 0x5a128fd8 },
    { "Q6_LWuw_add_mpy_hi_LhLuhLWuh(va, vb, pa)", 0xa7a992fe },
    { "Q6_LWw_mpyacc_LhLuh(pa, va, vb)", 0x32860054 },
    { "Q6_LWw_add_mpy_lo_LhLuhLWuh(va, vb, pa)", 0x2bb52f8b },
    { "Q6_LWw_mpyacc_LuhLuh(pa, va, vb)", 0x6e06afd3 },
    { "Q6_LWuw_add_mpy_hi_LuhLuhLWuh(va, vb, pa)", 0x7e000f51 },
    { "Q6_LWuw_add_mpy_lo_LuhLuhLWuh(va, vb, pa)", 0xf8b44e16 },
    { "Q6_LWw_add_mpy_hi_LuhLWhLWw(va, pa, pb)", 0x8d70f878 },
    { "Q6_LWuw_add_mpy_hi_LuhLWuhRw(va, pa, 0x8003fff7u)", 0x9e247fea },
    { "Q6_LWw_mpyacc_LhRh(pa, va, 0x8003fff7u)", 0x49c5558a },
    { "Q6_LWuw_add_mpy_hi_LhRuhLWuh(va, 0x8003fff7u, pa)", 0x81094382 },
    { "Q6_LWw_mpyacc_LhRuh(pa, va, 0x8003fff7u)", 0xf8af5c2e },
    { "Q6_LWw_add_mpy_lo_LhRuhLWuh(va, 0x8003fff7u, pa)", 0xfeadbfad },
    { "Q6_LWw_mpyacc_LuhRuh(pa, va, 0x8003fff7u)", 0x78eaea72 },
    { "Q6_LWuw_add_mpy_hi_LuhRuhLWuh(va, 0x8003fff7u, pa)", 0x19c9c382 },
    { "Q6_LWuw_add_mpy_lo_LuhRuhLWuh(va, 0x8003fff7u, pa)", 0x09c1eb27 },
    { "Q6_LWw_add_mpy_hi_LuhLWhRw(va, pa, 0x8003fff7u)", 0x441e6eb6 },
    { "Q6_LWuw_mpy_LuhLuh(va, vb)", 0x897f396a },
    { "Q6_LWw_mpy_LhLh(va, vb)", 0x3cd908a1 },
    { "Q6_LWw_mpy_LhLuh(va, vb)", 0x5f06285b },
    { "Q6_LWuw_mpy_LuhRuh(va, 0x8003fff7u)", 0x142eba14 },
    { "Q6_LWw_mpy_LhRh(va, 0x8003fff7u)", 0x8b1fd45c },
    { "Q6_LWw_mpy_LhRuh(va, 0x8003fff7u)", 0x96278c28 },
    { "Q6_Lh_mux_LhLhLh(va, vb, vd)", 0x3f84f4d3 },
    { "Q6_Lh_navg_LhLh(va, vb)", 0xb6e258de },
    { "Q6_Lh_navg_LuhLuh(va, vb)", 0x268358de },
    { "Q6_LWw_navg_LWuwLWuw(pa, pb)", 0xdac7a17f },
    { "Q6_LWw_navg_LWwLWw(pa, pb)", 0xedb5217f },
    { "Q6_L_not_L(va)", 0xec113827 },
    { "Q6_LW_not_LW(pa)", 0xa5cde948 },
    { "Q6_L_or_LL(va, vb)", 0x3479ea88 },
    { "Q6_LW_or_LWLW(pa, pb)", 0xcdcd4ac2 },
    { "Q6_L_orn_LL(va, vb)", 0x12daa3b2 },
    { "Q6_LW_orn_LWLW(pa, pb)", 0x69680f57 },
    { "Q6_Lh_shr_LhLh(va, vc)", 0xbcf72dec },
    { "Q6_Lh_shr_LWwLh_rnd_sat(pa, vc)", 0xceb3bba7 },
    { "Q6_Lh_shr_LWwLh_sat(pa, vc)", 0x2cb1f5ef },
    { "Q6_Lh_shr_LWwLuh(pa, vc)", 0x2c62cfec },
    { "Q6_Luh_shr_LuhLh(va, vc)", 0xc59d9a2d },
    { "Q6_Luh_shr_LWuwLh_rnd_sat(pa, vc)", 0x09f60d4f },
    { "Q6_Luh_shr_LWuwLh_sat(pa, vc)", 0x4604930b },
    { "Q6_Luh_shr_LWwLh_rnd_sat(pa, vc)", 0x90446497 },
    { "Q6_Luh_shr_LWwLh_sat(pa, vc)", 0x280313b6 },
    { "Q6_LWuw_shr_LWuwLh(pa, vc)", 0x4e213370 },
    { "Q6_LWw_shr_LWwLh(pa, vc)", 0x9cb55f22 },
    { "Q6_Lh_shracc_LhLh(va, vb, vc)", 0x79f50ab7 },
    { "Q6_Lh_shracc_LuhLh(va, vb, vc)", 0xdb4f251b },
    { "Q6_LWw_shracc_LWuwLh(pa, pb, vc)", 0x544f508c },
    { "Q6_LWw_shracc_LWwLh(pa, pb, vc)", 0xcf47b87d },
    { "Q6_Lh_shracc_LhRh(va, vb, 0x7ffefffbu)", 0x51b09f57 },
    { "Q6_Lh_shracc_LhRh(va, vb, 0x80010007u)", 0x292e5f78 },
    { "Q6_Lh_shracc_LuhRh(va, vb, 0x7ffefffbu)", 0x51b09f57 },
    { "Q6_Lh_shracc_LuhRh(va, vb, 0x80010007u)", 0x70a6bf78 },
    { "Q6_LWw_shracc_LWuwRh(pa, pb, 0x7ffefffbu)", 0xd70d3b57 },
    { "Q6_LWw_shracc_LWuwRh(pa, pb, 0x80010007u)", 0xe694b5e3 },
    { "Q6_LWw_shracc_LWwRh(pa, pb, 0x7ffefffbu)", 0xd70d3b57 },
    { "Q6_LWw_shracc_LWwRh(pa, pb, 0x80010007u)", 0xc9032de3 },
    { "Q6_Lh_shr_LhRh(va, 0x7ffefffbu)", 0x70ede845 },
    { "Q6_Lh_shr_LhRh(va, 0x80010007u)", 0x37a5547d },
    { "Q6_Lh_shr_LWwRh_rnd_sat(pa, 0x7ffefffbu)", 0x90219110 },
    { "Q6_Lh_shr_LWwRh_rnd_sat(pa, 0x80010007u)", 0xbb6803ae },
    { "Q6_Lh_shr_LWwRh_sat(pa, 0x7ffefffbu)", 0x90219110 },
    { "Q6_Lh_shr_LWwRh_sat(pa, 0x80010007u)", 0x949be419 },
    { "Q6_Lh_shr_LWwRuh(pa, 0x7ffefffbu)", 0xe02116d2 },
    { "Q6_Lh_shr_LWwRuh(pa, 0x80010007u)", 0x40ad898b },
    { "Q6_Luh_shr_LuhRh(va, 0x7ffefffbu)", 0x70ede845 },
    { "Q6_Luh_shr_LuhRh(va, 0x80010007u)", 0xbc0b287d },
    { "Q6_Luh_shr_LWuwRh_rnd_sat(pa, 0x7ffefffbu)", 0x4c7f29c5 },
    { "Q6_Luh_shr_LWuwRh_rnd_sat(pa, 0x80010007u)", 0x7c412f37 },
    { "Q6_Luh_shr_LWuwRh_sat(pa, 0x7ffefffbu)", 0x4c7f29c5 },
    { "Q6_Luh_shr_LWuwRh_sat(pa, 0x80010007u)", 0x7c412f37 },
    { "Q6_Luh_shr_LWwRh_rnd_sat(pa, 0x7ffefffbu)", 0xe1199110 },
    { "Q6_Luh_shr_LWwRh_rnd_sat(pa, 0x80010007u)", 0xf37903c2 },
    { "Q6_Luh_shr_LWwRh_sat(pa, 0x7ffefffbu)", 0xe1199110 },
    { "Q6_Luh_shr_LWwRh_sat(pa, 0x80010007u)", 0xf37903c2 },
    { "Q6_LWuw_shr_LWuwRh(pa, 0x7ffefffbu)", 0x19bae0d2 },
    { "Q6_LWuw_shr_LWuwRh(pa, 0x80010007u)", 0xd5a0f236 },
    { "Q6_LWw_shr_LWwRh(pa, 0x7ffefffbu)", 0x19bae0d2 },
    { "Q6_LWw_shr_LWwRh(pa, 0x80010007u)", 0xa20a4436 },
    { "Q6_Lh_sub_LhLh(va, vb)", 0xfafb43a8 },
    { "Q6_Lh_sub_LhLh_sat(va, vb)", 0x8b8ef26a },
    { "Q6_LWh_sub_LWhLWh(pa, pb)", 0xf28311d2 },
    { "Q6_LWh_sub_LWhLWh_sat(pa, pb)", 0x7656ec67 },
    { "Q6_Luh_sub_LuhLuh_sat(va, vb)", 0x557fb708 },
    { "Q6_Luh_sub_LhLh_sat(va, vb)", 0xde83c0e6 },
    { "Q6_LWuh_sub_LWhLWh_sat(pa, pb)", 0x6b4c37a4 },
    { "Q6_LWuh_sub_LWuhLWuh_sat(pa, pb)", 0x64e25576 },
    { "Q6_LWuw_sub_LWuwLWuw_sat(pa, pb)", 0x6df5cd38 },
    { "Q6_LWuw_sub_LWwLWw_sat(pa, pb)", 0xb6390227 },
    { "Q6_LWw_sub_LWwLWw(pa, pb)", 0x4b3aabf3 },
    { "Q6_LWw_sub_LWwLWw_sat(pa, pb)", 0xcbddbf89 },
    { "Q6_LWw_sub_LuhLuh(va, vb)", 0xf9a49ec1 },
    { "Q6_LWw_sub_LWwLh(pa, va)", 0x6076d35d },
    { "Q6_LWw_sub_LWwLuh(pa, va)", 0x0e272d6f },
    { "Q6_L_xor_LL(va, vb)", 0x8628eede },
    { "Q6_LW_xor_LWLW(pa, pb)", 0xaddd6d12 },
    { "Q6_Lh_equals_Is(3)", 0x7b4691c5 },
    { "Q6_Luh_equals_I(3)", 0x7b4691c5 },
    { "Q6_Lh_sub_ilog2_LhI(va, 3)", 0xbc6d0cf2 },
    { "Q6_Lh_sub_ilog2_LuhI(va, 3)", 0x34ae4b20 },
    { "Q6_Lh_sub_ilog2_LWuwI(pa, 3)", 0xc0c8e5dd },
    { "Q6_Lh_sub_ilog2_LWwI(pa, 3)", 0x6f45672a },
};

static int next_ref;

static void record(const char *name, uint32_t h)
{
    if (strcmp(refs[next_ref].name, name) != 0) {
        printf("FAIL table out of order at %s\n", name);
        exit(1);
    }
    if (refs[next_ref].hash != h) {
        printf("FAIL %s: expected 0x%08lx, got 0x%08lx\n", name,
               (unsigned long)refs[next_ref].hash, (unsigned long)h);
        err++;
    }
    next_ref++;
}

int main(void)
{
    init_vtcm();
    enable_hlx();
    init_inputs();

    RUN(Q6_Luh_abs_Lh(va));
    RUN(Q6_Lh_abs_Lh_sat(va));
    RUN(Q6_LWuh_abs_LWh(pa));
    RUN(Q6_LWh_abs_LWh_sat(pa));
    RUN(Q6_LWuw_abs_LWw(pa));
    RUN(Q6_LWw_abs_LWw_sat(pa));
    RUN(Q6_Luh_absdiff_LhLh(va, vb));
    RUN(Q6_Luh_absdiff_LuhLuh(va, vb));
    RUN(Q6_LWuw_absdiff_LWuwLWuw(pa, pb));
    RUN(Q6_LWuw_absdiff_LWwLWw(pa, pb));
    RUN(Q6_Lh_add_LhLh(va, vb));
    RUN(Q6_Lh_add_LhLh_sat(va, vb));
    RUN(Q6_LWh_add_LWhLWh(pa, pb));
    RUN(Q6_LWh_add_LWhLWh_sat(pa, pb));
    RUN(Q6_Luh_add_LuhLuh_sat(va, vb));
    RUN(Q6_Luh_add_LhLh_sat(va, vb));
    RUN(Q6_LWuh_add_LWhLWh_sat(pa, pb));
    RUN(Q6_LWuh_add_LWuhLWuh_sat(pa, pb));
    RUN(Q6_LWuw_add_LWuwLWuw_sat(pa, pb));
    RUN(Q6_LWuw_add_LWwLWw_sat(pa, pb));
    RUN(Q6_LWw_add_LWwLWw(pa, pb));
    RUN(Q6_LWw_add_LWwLWw_sat(pa, pb));
    RUN(Q6_LWw_add_LuhLuh(va, vb));
    RUN(Q6_LWw_add_LWwLh(pa, va));
    RUN(Q6_LWw_add_LWwLuh(pa, va));
    RUN(Q6_L_and_LL(va, vb));
    RUN(Q6_LW_and_LWLW(pa, pb));
    RUN(Q6_L_andn_LL(va, vb));
    RUN(Q6_LW_andn_LWLW(pa, pb));
    RUN(Q6_Lh_equals_Lh(va));
    RUN(Q6_Lh_equals_Rh(0x8003fff7u));
    RUN(Q6_LWw_equals_Rw(0x8003fff7u));
    RUN(Q6_Lh_avg_LhLh(va, vb));
    RUN(Q6_Lh_avg_LhLh_rnd(va, vb));
    RUN(Q6_Luh_avg_LuhLuh(va, vb));
    RUN(Q6_Luh_avg_LuhLuh_rnd(va, vb));
    RUN(Q6_LWuw_avg_LWuwLWuw(pa, pb));
    RUN(Q6_LWuw_avg_LWuwLWuw_rnd(pa, pb));
    RUN(Q6_LWw_avg_LWwLWw(pa, pb));
    RUN(Q6_LWw_avg_LWwLWw_rnd(pa, pb));
    RUN(Q6_Lh_cmp_gt_LhLh(va, vb));
    RUN(Q6_LWh_cmp_gt_LWhLWh(pa, pb));
    RUN(Q6_Lh_cmp_gt_LuhLuh(va, vb));
    RUN(Q6_LWh_cmp_gt_LWuhLWuh(pa, pb));
    RUN(Q6_LWw_cmp_gt_LWuwLWuw(pa, pb));
    RUN(Q6_LWw_cmp_gt_LWwLWw(pa, pb));
    RUN(Q6_Lb_packhi_LhLh(va, vb));
    RUN(Q6_Lb_packlo_LhLh(va, vb));
    RUN(Q6_LWh_unpackhi_Lb(va));
    RUN(Q6_LWh_unpacklo_Lb(va));
    RUN(Q6_LWuw_zxt_Luh(va));
    RUN(Q6_LWw_sxt_Lh(va));
    RUN(Q6_Lh_lut4_LuhPh(va, 0x8001400220037ffcull));
    RUN(Q6_Lh_lut4_LuhPh_mask(va, 0x8001400220037ffcull));
    RUN(Q6_LWw_lut4_LuhPuh_rs1(va, 0x8001400220037ffcull));
    RUN(Q6_LWw_lut4_LuhPuh_rs1_neg(va, 0x8001400220037ffcull));
    RUN(Q6_Lh_max_LhLh(va, vb));
    RUN(Q6_LWh_max_LWhLWh(pa, pb));
    RUN(Q6_Luh_max_LuhLuh(va, vb));
    RUN(Q6_LWuh_max_LWuhLWuh(pa, pb));
    RUN(Q6_LWuw_max_LWuwLWuw(pa, pb));
    RUN(Q6_LWw_max_LWwLWw(pa, pb));
    RUN(Q6_Lh_min_LhLh(va, vb));
    RUN(Q6_LWh_min_LWhLWh(pa, pb));
    RUN(Q6_Luh_min_LuhLuh(va, vb));
    RUN(Q6_LWuh_min_LWuhLWuh(pa, pb));
    RUN(Q6_LWuw_min_LWuwLWuw(pa, pb));
    RUN(Q6_LWw_min_LWwLWw(pa, pb));
    RUN(Q6_LWuw_add_mpy_hi_LuhLWuhLWw(va, pa, pb));
    RUN(Q6_LWw_mpyacc_LhLh(pa, va, vb));
    RUN(Q6_LWuw_add_mpy_hi_LhLuhLWuh(va, vb, pa));
    RUN(Q6_LWw_mpyacc_LhLuh(pa, va, vb));
    RUN(Q6_LWw_add_mpy_lo_LhLuhLWuh(va, vb, pa));
    RUN(Q6_LWw_mpyacc_LuhLuh(pa, va, vb));
    RUN(Q6_LWuw_add_mpy_hi_LuhLuhLWuh(va, vb, pa));
    RUN(Q6_LWuw_add_mpy_lo_LuhLuhLWuh(va, vb, pa));
    RUN(Q6_LWw_add_mpy_hi_LuhLWhLWw(va, pa, pb));
    RUN(Q6_LWuw_add_mpy_hi_LuhLWuhRw(va, pa, 0x8003fff7u));
    RUN(Q6_LWw_mpyacc_LhRh(pa, va, 0x8003fff7u));
    RUN(Q6_LWuw_add_mpy_hi_LhRuhLWuh(va, 0x8003fff7u, pa));
    RUN(Q6_LWw_mpyacc_LhRuh(pa, va, 0x8003fff7u));
    RUN(Q6_LWw_add_mpy_lo_LhRuhLWuh(va, 0x8003fff7u, pa));
    RUN(Q6_LWw_mpyacc_LuhRuh(pa, va, 0x8003fff7u));
    RUN(Q6_LWuw_add_mpy_hi_LuhRuhLWuh(va, 0x8003fff7u, pa));
    RUN(Q6_LWuw_add_mpy_lo_LuhRuhLWuh(va, 0x8003fff7u, pa));
    RUN(Q6_LWw_add_mpy_hi_LuhLWhRw(va, pa, 0x8003fff7u));
    RUN(Q6_LWuw_mpy_LuhLuh(va, vb));
    RUN(Q6_LWw_mpy_LhLh(va, vb));
    RUN(Q6_LWw_mpy_LhLuh(va, vb));
    RUN(Q6_LWuw_mpy_LuhRuh(va, 0x8003fff7u));
    RUN(Q6_LWw_mpy_LhRh(va, 0x8003fff7u));
    RUN(Q6_LWw_mpy_LhRuh(va, 0x8003fff7u));
    RUN(Q6_Lh_mux_LhLhLh(va, vb, vd));
    RUN(Q6_Lh_navg_LhLh(va, vb));
    RUN(Q6_Lh_navg_LuhLuh(va, vb));
    RUN(Q6_LWw_navg_LWuwLWuw(pa, pb));
    RUN(Q6_LWw_navg_LWwLWw(pa, pb));
    RUN(Q6_L_not_L(va));
    RUN(Q6_LW_not_LW(pa));
    RUN(Q6_L_or_LL(va, vb));
    RUN(Q6_LW_or_LWLW(pa, pb));
    RUN(Q6_L_orn_LL(va, vb));
    RUN(Q6_LW_orn_LWLW(pa, pb));
    RUN(Q6_Lh_shr_LhLh(va, vc));
    RUN(Q6_Lh_shr_LWwLh_rnd_sat(pa, vc));
    RUN(Q6_Lh_shr_LWwLh_sat(pa, vc));
    RUN(Q6_Lh_shr_LWwLuh(pa, vc));
    RUN(Q6_Luh_shr_LuhLh(va, vc));
    RUN(Q6_Luh_shr_LWuwLh_rnd_sat(pa, vc));
    RUN(Q6_Luh_shr_LWuwLh_sat(pa, vc));
    RUN(Q6_Luh_shr_LWwLh_rnd_sat(pa, vc));
    RUN(Q6_Luh_shr_LWwLh_sat(pa, vc));
    RUN(Q6_LWuw_shr_LWuwLh(pa, vc));
    RUN(Q6_LWw_shr_LWwLh(pa, vc));
    RUN(Q6_Lh_shracc_LhLh(va, vb, vc));
    RUN(Q6_Lh_shracc_LuhLh(va, vb, vc));
    RUN(Q6_LWw_shracc_LWuwLh(pa, pb, vc));
    RUN(Q6_LWw_shracc_LWwLh(pa, pb, vc));
    RUN(Q6_Lh_shracc_LhRh(va, vb, 0x7ffefffbu));
    RUN(Q6_Lh_shracc_LhRh(va, vb, 0x80010007u));
    RUN(Q6_Lh_shracc_LuhRh(va, vb, 0x7ffefffbu));
    RUN(Q6_Lh_shracc_LuhRh(va, vb, 0x80010007u));
    RUN(Q6_LWw_shracc_LWuwRh(pa, pb, 0x7ffefffbu));
    RUN(Q6_LWw_shracc_LWuwRh(pa, pb, 0x80010007u));
    RUN(Q6_LWw_shracc_LWwRh(pa, pb, 0x7ffefffbu));
    RUN(Q6_LWw_shracc_LWwRh(pa, pb, 0x80010007u));
    RUN(Q6_Lh_shr_LhRh(va, 0x7ffefffbu));
    RUN(Q6_Lh_shr_LhRh(va, 0x80010007u));
    RUN(Q6_Lh_shr_LWwRh_rnd_sat(pa, 0x7ffefffbu));
    RUN(Q6_Lh_shr_LWwRh_rnd_sat(pa, 0x80010007u));
    RUN(Q6_Lh_shr_LWwRh_sat(pa, 0x7ffefffbu));
    RUN(Q6_Lh_shr_LWwRh_sat(pa, 0x80010007u));
    RUN(Q6_Lh_shr_LWwRuh(pa, 0x7ffefffbu));
    RUN(Q6_Lh_shr_LWwRuh(pa, 0x80010007u));
    RUN(Q6_Luh_shr_LuhRh(va, 0x7ffefffbu));
    RUN(Q6_Luh_shr_LuhRh(va, 0x80010007u));
    RUN(Q6_Luh_shr_LWuwRh_rnd_sat(pa, 0x7ffefffbu));
    RUN(Q6_Luh_shr_LWuwRh_rnd_sat(pa, 0x80010007u));
    RUN(Q6_Luh_shr_LWuwRh_sat(pa, 0x7ffefffbu));
    RUN(Q6_Luh_shr_LWuwRh_sat(pa, 0x80010007u));
    RUN(Q6_Luh_shr_LWwRh_rnd_sat(pa, 0x7ffefffbu));
    RUN(Q6_Luh_shr_LWwRh_rnd_sat(pa, 0x80010007u));
    RUN(Q6_Luh_shr_LWwRh_sat(pa, 0x7ffefffbu));
    RUN(Q6_Luh_shr_LWwRh_sat(pa, 0x80010007u));
    RUN(Q6_LWuw_shr_LWuwRh(pa, 0x7ffefffbu));
    RUN(Q6_LWuw_shr_LWuwRh(pa, 0x80010007u));
    RUN(Q6_LWw_shr_LWwRh(pa, 0x7ffefffbu));
    RUN(Q6_LWw_shr_LWwRh(pa, 0x80010007u));
    RUN(Q6_Lh_sub_LhLh(va, vb));
    RUN(Q6_Lh_sub_LhLh_sat(va, vb));
    RUN(Q6_LWh_sub_LWhLWh(pa, pb));
    RUN(Q6_LWh_sub_LWhLWh_sat(pa, pb));
    RUN(Q6_Luh_sub_LuhLuh_sat(va, vb));
    RUN(Q6_Luh_sub_LhLh_sat(va, vb));
    RUN(Q6_LWuh_sub_LWhLWh_sat(pa, pb));
    RUN(Q6_LWuh_sub_LWuhLWuh_sat(pa, pb));
    RUN(Q6_LWuw_sub_LWuwLWuw_sat(pa, pb));
    RUN(Q6_LWuw_sub_LWwLWw_sat(pa, pb));
    RUN(Q6_LWw_sub_LWwLWw(pa, pb));
    RUN(Q6_LWw_sub_LWwLWw_sat(pa, pb));
    RUN(Q6_LWw_sub_LuhLuh(va, vb));
    RUN(Q6_LWw_sub_LWwLh(pa, va));
    RUN(Q6_LWw_sub_LWwLuh(pa, va));
    RUN(Q6_L_xor_LL(va, vb));
    RUN(Q6_LW_xor_LWLW(pa, pb));
    RUN(Q6_Lh_equals_Is(3));
    RUN(Q6_Luh_equals_I(3));
    RUN(Q6_Lh_sub_ilog2_LhI(va, 3));
    RUN(Q6_Lh_sub_ilog2_LuhI(va, 3));
    RUN(Q6_Lh_sub_ilog2_LWuwI(pa, 3));
    RUN(Q6_Lh_sub_ilog2_LWwI(pa, 3));

    puts(err ? "FAIL" : "PASS");
    return err;
}
