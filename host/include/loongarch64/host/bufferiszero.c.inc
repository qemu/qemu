/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * buffer_is_zero acceleration, loongarch64 version.
 */

/*
 * Builtins for LSX and LASX are introduced by gcc 14 and llvm 18,
 * but as yet neither has support for attribute target, so neither
 * is able to enable the optimization without globally enabling
 * vector support.  Since we want runtime detection, use assembly.
 */

static bool buffer_is_zero_lsx(const void *buf, size_t len)
{
    const void *p = QEMU_ALIGN_PTR_DOWN(buf + 16, 16);
    const void *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 16) - (7 * 16);
    const void *l = buf + len;
    bool ret;

    asm("vld $vr0,%2,0\n\t"             /* first: buf + 0 */
        "vld $vr1,%4,-16\n\t"           /* last: buf + len - 16 */
        "vld $vr2,%3,0\n\t"             /* e[0] */
        "vld $vr3,%3,16\n\t"            /* e[1] */
        "vld $vr4,%3,32\n\t"            /* e[2] */
        "vld $vr5,%3,48\n\t"            /* e[3] */
        "vld $vr6,%3,64\n\t"            /* e[4] */
        "vld $vr7,%3,80\n\t"            /* e[5] */
        "vld $vr8,%3,96\n\t"            /* e[6] */
        "vor.v $vr0,$vr0,$vr1\n\t"
        "vor.v $vr2,$vr2,$vr3\n\t"
        "vor.v $vr4,$vr4,$vr5\n\t"
        "vor.v $vr6,$vr6,$vr7\n\t"
        "vor.v $vr0,$vr0,$vr2\n\t"
        "vor.v $vr4,$vr4,$vr6\n\t"
        "vor.v $vr0,$vr0,$vr4\n\t"
        "vor.v $vr0,$vr0,$vr8\n\t"
        "or %0,$r0,$r0\n"               /* prepare return false */
    "1:\n\t"
        "vsetnez.v $fcc0,$vr0\n\t"
        "bcnez $fcc0,2f\n\t"
        "vld $vr0,%1,0\n\t"             /* p[0] */
        "vld $vr1,%1,16\n\t"            /* p[1] */
        "vld $vr2,%1,32\n\t"            /* p[2] */
        "vld $vr3,%1,48\n\t"            /* p[3] */
        "vld $vr4,%1,64\n\t"            /* p[4] */
        "vld $vr5,%1,80\n\t"            /* p[5] */
        "vld $vr6,%1,96\n\t"            /* p[6] */
        "vld $vr7,%1,112\n\t"           /* p[7] */
        "addi.d %1,%1,128\n\t"
        "vor.v $vr0,$vr0,$vr1\n\t"
        "vor.v $vr2,$vr2,$vr3\n\t"
        "vor.v $vr4,$vr4,$vr5\n\t"
        "vor.v $vr6,$vr6,$vr7\n\t"
        "vor.v $vr0,$vr0,$vr2\n\t"
        "vor.v $vr4,$vr4,$vr6\n\t"
        "vor.v $vr0,$vr0,$vr4\n\t"
        "bltu %1,%3,1b\n\t"
        "vsetnez.v $fcc0,$vr0\n\t"
        "bcnez $fcc0,2f\n\t"
        "ori %0,$r0,1\n"
    "2:"
        : "=&r"(ret), "+r"(p)
        : "r"(buf), "r"(e), "r"(l)
        : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "$f8",
          "$fcc0");

    return ret;
}

static bool buffer_is_zero_lasx(const void *buf, size_t len)
{
    const void *p = QEMU_ALIGN_PTR_DOWN(buf + 32, 32);
    const void *e = QEMU_ALIGN_PTR_DOWN(buf + len - 1, 32) - (7 * 32);
    const void *l = buf + len;
    bool ret;

    asm("xvld $xr0,%2,0\n\t"             /* first: buf + 0 */
        "xvld $xr1,%4,-32\n\t"           /* last: buf + len - 32 */
        "xvld $xr2,%3,0\n\t"             /* e[0] */
        "xvld $xr3,%3,32\n\t"            /* e[1] */
        "xvld $xr4,%3,64\n\t"            /* e[2] */
        "xvld $xr5,%3,96\n\t"            /* e[3] */
        "xvld $xr6,%3,128\n\t"           /* e[4] */
        "xvld $xr7,%3,160\n\t"           /* e[5] */
        "xvld $xr8,%3,192\n\t"           /* e[6] */
        "xvor.v $xr0,$xr0,$xr1\n\t"
        "xvor.v $xr2,$xr2,$xr3\n\t"
        "xvor.v $xr4,$xr4,$xr5\n\t"
        "xvor.v $xr6,$xr6,$xr7\n\t"
        "xvor.v $xr0,$xr0,$xr2\n\t"
        "xvor.v $xr4,$xr4,$xr6\n\t"
        "xvor.v $xr0,$xr0,$xr4\n\t"
        "xvor.v $xr0,$xr0,$xr8\n\t"
        "or %0,$r0,$r0\n\t"              /* prepare return false */
        "bgeu %1,%3,2f\n"
    "1:\n\t"
        "xvsetnez.v $fcc0,$xr0\n\t"
        "bcnez $fcc0,3f\n\t"
        "xvld $xr0,%1,0\n\t"             /* p[0] */
        "xvld $xr1,%1,32\n\t"            /* p[1] */
        "xvld $xr2,%1,64\n\t"            /* p[2] */
        "xvld $xr3,%1,96\n\t"            /* p[3] */
        "xvld $xr4,%1,128\n\t"           /* p[4] */
        "xvld $xr5,%1,160\n\t"           /* p[5] */
        "xvld $xr6,%1,192\n\t"           /* p[6] */
        "xvld $xr7,%1,224\n\t"           /* p[7] */
        "addi.d %1,%1,256\n\t"
        "xvor.v $xr0,$xr0,$xr1\n\t"
        "xvor.v $xr2,$xr2,$xr3\n\t"
        "xvor.v $xr4,$xr4,$xr5\n\t"
        "xvor.v $xr6,$xr6,$xr7\n\t"
        "xvor.v $xr0,$xr0,$xr2\n\t"
        "xvor.v $xr4,$xr4,$xr6\n\t"
        "xvor.v $xr0,$xr0,$xr4\n\t"
        "bltu %1,%3,1b\n"
    "2:\n\t"
        "xvsetnez.v $fcc0,$xr0\n\t"
        "bcnez $fcc0,3f\n\t"
        "ori %0,$r0,1\n"
    "3:"
        : "=&r"(ret), "+r"(p)
        : "r"(buf), "r"(e), "r"(l)
        : "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "$f8",
          "$fcc0");

    return ret;
}

static biz_accel_fn const accel_table[] = {
    buffer_is_zero_int_ge256,
    buffer_is_zero_lsx,
    buffer_is_zero_lasx,
};

static unsigned best_accel(void)
{
    unsigned info = cpuinfo_init();
    if (info & CPUINFO_LASX) {
        return 2;
    }
    if (info & CPUINFO_LSX) {
        return 1;
    }
    return 0;
}
