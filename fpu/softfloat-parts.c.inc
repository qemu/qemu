/*
 * QEMU float support
 *
 * The code in this source file is derived from release 2a of the SoftFloat
 * IEC/IEEE Floating-point Arithmetic Package. Those parts of the code (and
 * some later contributions) are provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file after December 1st 2014 will be
 * taken to be licensed under the Softfloat-2a license unless specifically
 * indicated otherwise.
 */

static void partsN(return_nan)(FloatPartsN *a, float_status *s)
{
    switch (a->cls) {
    case float_class_snan:
        float_raise(float_flag_invalid, s);
        if (s->default_nan_mode) {
            parts_default_nan(a, s);
        } else {
            parts_silence_nan(a, s);
        }
        break;
    case float_class_qnan:
        if (s->default_nan_mode) {
            parts_default_nan(a, s);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static FloatPartsN *partsN(pick_nan)(FloatPartsN *a, FloatPartsN *b,
                                     float_status *s)
{
    if (is_snan(a->cls) || is_snan(b->cls)) {
        float_raise(float_flag_invalid, s);
    }

    if (s->default_nan_mode) {
        parts_default_nan(a, s);
    } else {
        int cmp = frac_cmp(a, b);
        if (cmp == 0) {
            cmp = a->sign < b->sign;
        }

        if (pickNaN(a->cls, b->cls, cmp > 0, s)) {
            a = b;
        }
        if (is_snan(a->cls)) {
            parts_silence_nan(a, s);
        }
    }
    return a;
}

static FloatPartsN *partsN(pick_nan_muladd)(FloatPartsN *a, FloatPartsN *b,
                                            FloatPartsN *c, float_status *s,
                                            int ab_mask, int abc_mask)
{
    int which;

    if (unlikely(abc_mask & float_cmask_snan)) {
        float_raise(float_flag_invalid, s);
    }

    which = pickNaNMulAdd(a->cls, b->cls, c->cls,
                          ab_mask == float_cmask_infzero, s);

    if (s->default_nan_mode || which == 3) {
        /*
         * Note that this check is after pickNaNMulAdd so that function
         * has an opportunity to set the Invalid flag for infzero.
         */
        parts_default_nan(a, s);
        return a;
    }

    switch (which) {
    case 0:
        break;
    case 1:
        a = b;
        break;
    case 2:
        a = c;
        break;
    default:
        g_assert_not_reached();
    }
    if (is_snan(a->cls)) {
        parts_silence_nan(a, s);
    }
    return a;
}
