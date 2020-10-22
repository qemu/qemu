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

/*
 * Canonicalize the FloatParts structure.  Determine the class,
 * unbias the exponent, and normalize the fraction.
 */
static void partsN(canonicalize)(FloatPartsN *p, float_status *status,
                                 const FloatFmt *fmt)
{
    if (unlikely(p->exp == 0)) {
        if (likely(frac_eqz(p))) {
            p->cls = float_class_zero;
        } else if (status->flush_inputs_to_zero) {
            float_raise(float_flag_input_denormal, status);
            p->cls = float_class_zero;
            frac_clear(p);
        } else {
            int shift = frac_normalize(p);
            p->cls = float_class_normal;
            p->exp = fmt->frac_shift - fmt->exp_bias - shift + 1;
        }
    } else if (likely(p->exp < fmt->exp_max) || fmt->arm_althp) {
        p->cls = float_class_normal;
        p->exp -= fmt->exp_bias;
        frac_shl(p, fmt->frac_shift);
        p->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
    } else if (likely(frac_eqz(p))) {
        p->cls = float_class_inf;
    } else {
        frac_shl(p, fmt->frac_shift);
        p->cls = (parts_is_snan_frac(p->frac_hi, status)
                  ? float_class_snan : float_class_qnan);
    }
}

/*
 * Round and uncanonicalize a floating-point number by parts. There
 * are FRAC_SHIFT bits that may require rounding at the bottom of the
 * fraction; these bits will be removed. The exponent will be biased
 * by EXP_BIAS and must be bounded by [EXP_MAX-1, 0].
 */
static void partsN(uncanon)(FloatPartsN *p, float_status *s,
                            const FloatFmt *fmt)
{
    const int exp_max = fmt->exp_max;
    const int frac_shift = fmt->frac_shift;
    const uint64_t frac_lsb = fmt->frac_lsb;
    const uint64_t frac_lsbm1 = fmt->frac_lsbm1;
    const uint64_t round_mask = fmt->round_mask;
    const uint64_t roundeven_mask = fmt->roundeven_mask;
    uint64_t inc;
    bool overflow_norm;
    int exp, flags = 0;

    if (unlikely(p->cls != float_class_normal)) {
        switch (p->cls) {
        case float_class_zero:
            p->exp = 0;
            frac_clear(p);
            return;
        case float_class_inf:
            g_assert(!fmt->arm_althp);
            p->exp = fmt->exp_max;
            frac_clear(p);
            return;
        case float_class_qnan:
        case float_class_snan:
            g_assert(!fmt->arm_althp);
            p->exp = fmt->exp_max;
            frac_shr(p, fmt->frac_shift);
            return;
        default:
            break;
        }
        g_assert_not_reached();
    }

    switch (s->float_rounding_mode) {
    case float_round_nearest_even:
        overflow_norm = false;
        inc = ((p->frac_lo & roundeven_mask) != frac_lsbm1 ? frac_lsbm1 : 0);
        break;
    case float_round_ties_away:
        overflow_norm = false;
        inc = frac_lsbm1;
        break;
    case float_round_to_zero:
        overflow_norm = true;
        inc = 0;
        break;
    case float_round_up:
        inc = p->sign ? 0 : round_mask;
        overflow_norm = p->sign;
        break;
    case float_round_down:
        inc = p->sign ? round_mask : 0;
        overflow_norm = !p->sign;
        break;
    case float_round_to_odd:
        overflow_norm = true;
        inc = p->frac_lo & frac_lsb ? 0 : round_mask;
        break;
    default:
        g_assert_not_reached();
    }

    exp = p->exp + fmt->exp_bias;
    if (likely(exp > 0)) {
        if (p->frac_lo & round_mask) {
            flags |= float_flag_inexact;
            if (frac_addi(p, p, inc)) {
                frac_shr(p, 1);
                p->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
                exp++;
            }
        }
        frac_shr(p, frac_shift);

        if (fmt->arm_althp) {
            /* ARM Alt HP eschews Inf and NaN for a wider exponent.  */
            if (unlikely(exp > exp_max)) {
                /* Overflow.  Return the maximum normal.  */
                flags = float_flag_invalid;
                exp = exp_max;
                frac_allones(p);
            }
        } else if (unlikely(exp >= exp_max)) {
            flags |= float_flag_overflow | float_flag_inexact;
            if (overflow_norm) {
                exp = exp_max - 1;
                frac_allones(p);
            } else {
                p->cls = float_class_inf;
                exp = exp_max;
                frac_clear(p);
            }
        }
    } else if (s->flush_to_zero) {
        flags |= float_flag_output_denormal;
        p->cls = float_class_zero;
        exp = 0;
        frac_clear(p);
    } else {
        bool is_tiny = s->tininess_before_rounding || exp < 0;

        if (!is_tiny) {
            FloatPartsN discard;
            is_tiny = !frac_addi(&discard, p, inc);
        }

        frac_shrjam(p, 1 - exp);

        if (p->frac_lo & round_mask) {
            /* Need to recompute round-to-even/round-to-odd. */
            switch (s->float_rounding_mode) {
            case float_round_nearest_even:
                inc = ((p->frac_lo & roundeven_mask) != frac_lsbm1
                       ? frac_lsbm1 : 0);
                break;
            case float_round_to_odd:
                inc = p->frac_lo & frac_lsb ? 0 : round_mask;
                break;
            default:
                break;
            }
            flags |= float_flag_inexact;
            frac_addi(p, p, inc);
        }

        exp = (p->frac_hi & DECOMPOSED_IMPLICIT_BIT) != 0;
        frac_shr(p, frac_shift);

        if (is_tiny && (flags & float_flag_inexact)) {
            flags |= float_flag_underflow;
        }
        if (exp == 0 && frac_eqz(p)) {
            p->cls = float_class_zero;
        }
    }
    p->exp = exp;
    float_raise(flags, s);
}

/*
 * Returns the result of adding or subtracting the values of the
 * floating-point values `a' and `b'. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 */
static FloatPartsN *partsN(addsub)(FloatPartsN *a, FloatPartsN *b,
                                   float_status *s, bool subtract)
{
    bool b_sign = b->sign ^ subtract;
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);

    if (a->sign != b_sign) {
        /* Subtraction */
        if (likely(ab_mask == float_cmask_normal)) {
            if (parts_sub_normal(a, b)) {
                return a;
            }
            /* Subtract was exact, fall through to set sign. */
            ab_mask = float_cmask_zero;
        }

        if (ab_mask == float_cmask_zero) {
            a->sign = s->float_rounding_mode == float_round_down;
            return a;
        }

        if (unlikely(ab_mask & float_cmask_anynan)) {
            goto p_nan;
        }

        if (ab_mask & float_cmask_inf) {
            if (a->cls != float_class_inf) {
                /* N - Inf */
                goto return_b;
            }
            if (b->cls != float_class_inf) {
                /* Inf - N */
                return a;
            }
            /* Inf - Inf */
            float_raise(float_flag_invalid, s);
            parts_default_nan(a, s);
            return a;
        }
    } else {
        /* Addition */
        if (likely(ab_mask == float_cmask_normal)) {
            parts_add_normal(a, b);
            return a;
        }

        if (ab_mask == float_cmask_zero) {
            return a;
        }

        if (unlikely(ab_mask & float_cmask_anynan)) {
            goto p_nan;
        }

        if (ab_mask & float_cmask_inf) {
            a->cls = float_class_inf;
            return a;
        }
    }

    if (b->cls == float_class_zero) {
        g_assert(a->cls == float_class_normal);
        return a;
    }

    g_assert(a->cls == float_class_zero);
    g_assert(b->cls == float_class_normal);
 return_b:
    b->sign = b_sign;
    return b;

 p_nan:
    return parts_pick_nan(a, b, s);
}
