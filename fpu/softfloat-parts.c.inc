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

    overflow_norm = false;
    switch (s->float_rounding_mode) {
    case float_round_nearest_even:
        inc = ((p->frac_lo & roundeven_mask) != frac_lsbm1 ? frac_lsbm1 : 0);
        break;
    case float_round_ties_away:
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
        /* fall through */
    case float_round_to_odd_inf:
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
            case float_round_to_odd_inf:
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

/*
 * Returns the result of multiplying the floating-point values `a' and
 * `b'. The operation is performed according to the IEC/IEEE Standard
 * for Binary Floating-Point Arithmetic.
 */
static FloatPartsN *partsN(mul)(FloatPartsN *a, FloatPartsN *b,
                                float_status *s)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    bool sign = a->sign ^ b->sign;

    if (likely(ab_mask == float_cmask_normal)) {
        FloatPartsW tmp;

        frac_mulw(&tmp, a, b);
        frac_truncjam(a, &tmp);

        a->exp += b->exp + 1;
        if (!(a->frac_hi & DECOMPOSED_IMPLICIT_BIT)) {
            frac_add(a, a, a);
            a->exp -= 1;
        }

        a->sign = sign;
        return a;
    }

    /* Inf * Zero == NaN */
    if (unlikely(ab_mask == float_cmask_infzero)) {
        float_raise(float_flag_invalid, s);
        parts_default_nan(a, s);
        return a;
    }

    if (unlikely(ab_mask & float_cmask_anynan)) {
        return parts_pick_nan(a, b, s);
    }

    /* Multiply by 0 or Inf */
    if (ab_mask & float_cmask_inf) {
        a->cls = float_class_inf;
        a->sign = sign;
        return a;
    }

    g_assert(ab_mask & float_cmask_zero);
    a->cls = float_class_zero;
    a->sign = sign;
    return a;
}

/*
 * Returns the result of multiplying the floating-point values `a' and
 * `b' then adding 'c', with no intermediate rounding step after the
 * multiplication. The operation is performed according to the
 * IEC/IEEE Standard for Binary Floating-Point Arithmetic 754-2008.
 * The flags argument allows the caller to select negation of the
 * addend, the intermediate product, or the final result. (The
 * difference between this and having the caller do a separate
 * negation is that negating externally will flip the sign bit on NaNs.)
 *
 * Requires A and C extracted into a double-sized structure to provide the
 * extra space for the widening multiply.
 */
static FloatPartsN *partsN(muladd)(FloatPartsN *a, FloatPartsN *b,
                                   FloatPartsN *c, int flags, float_status *s)
{
    int ab_mask, abc_mask;
    FloatPartsW p_widen, c_widen;

    ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    abc_mask = float_cmask(c->cls) | ab_mask;

    /*
     * It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (unlikely(abc_mask & float_cmask_anynan)) {
        return parts_pick_nan_muladd(a, b, c, s, ab_mask, abc_mask);
    }

    if (flags & float_muladd_negate_c) {
        c->sign ^= 1;
    }

    /* Compute the sign of the product into A. */
    a->sign ^= b->sign;
    if (flags & float_muladd_negate_product) {
        a->sign ^= 1;
    }

    if (unlikely(ab_mask != float_cmask_normal)) {
        if (unlikely(ab_mask == float_cmask_infzero)) {
            goto d_nan;
        }

        if (ab_mask & float_cmask_inf) {
            if (c->cls == float_class_inf && a->sign != c->sign) {
                goto d_nan;
            }
            goto return_inf;
        }

        g_assert(ab_mask & float_cmask_zero);
        if (c->cls == float_class_normal) {
            *a = *c;
            goto return_normal;
        }
        if (c->cls == float_class_zero) {
            if (a->sign != c->sign) {
                goto return_sub_zero;
            }
            goto return_zero;
        }
        g_assert(c->cls == float_class_inf);
    }

    if (unlikely(c->cls == float_class_inf)) {
        a->sign = c->sign;
        goto return_inf;
    }

    /* Perform the multiplication step. */
    p_widen.sign = a->sign;
    p_widen.exp = a->exp + b->exp + 1;
    frac_mulw(&p_widen, a, b);
    if (!(p_widen.frac_hi & DECOMPOSED_IMPLICIT_BIT)) {
        frac_add(&p_widen, &p_widen, &p_widen);
        p_widen.exp -= 1;
    }

    /* Perform the addition step. */
    if (c->cls != float_class_zero) {
        /* Zero-extend C to less significant bits. */
        frac_widen(&c_widen, c);
        c_widen.exp = c->exp;

        if (a->sign == c->sign) {
            parts_add_normal(&p_widen, &c_widen);
        } else if (!parts_sub_normal(&p_widen, &c_widen)) {
            goto return_sub_zero;
        }
    }

    /* Narrow with sticky bit, for proper rounding later. */
    frac_truncjam(a, &p_widen);
    a->sign = p_widen.sign;
    a->exp = p_widen.exp;

 return_normal:
    if (flags & float_muladd_halve_result) {
        a->exp -= 1;
    }
 finish_sign:
    if (flags & float_muladd_negate_result) {
        a->sign ^= 1;
    }
    return a;

 return_sub_zero:
    a->sign = s->float_rounding_mode == float_round_down;
 return_zero:
    a->cls = float_class_zero;
    goto finish_sign;

 return_inf:
    a->cls = float_class_inf;
    goto finish_sign;

 d_nan:
    float_raise(float_flag_invalid, s);
    parts_default_nan(a, s);
    return a;
}

/*
 * Returns the result of dividing the floating-point value `a' by the
 * corresponding value `b'. The operation is performed according to
 * the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */
static FloatPartsN *partsN(div)(FloatPartsN *a, FloatPartsN *b,
                                float_status *s)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    bool sign = a->sign ^ b->sign;

    if (likely(ab_mask == float_cmask_normal)) {
        a->sign = sign;
        a->exp -= b->exp + frac_div(a, b);
        return a;
    }

    /* 0/0 or Inf/Inf => NaN */
    if (unlikely(ab_mask == float_cmask_zero) ||
        unlikely(ab_mask == float_cmask_inf)) {
        float_raise(float_flag_invalid, s);
        parts_default_nan(a, s);
        return a;
    }

    /* All the NaN cases */
    if (unlikely(ab_mask & float_cmask_anynan)) {
        return parts_pick_nan(a, b, s);
    }

    a->sign = sign;

    /* Inf / X */
    if (a->cls == float_class_inf) {
        return a;
    }

    /* 0 / X */
    if (a->cls == float_class_zero) {
        return a;
    }

    /* X / Inf */
    if (b->cls == float_class_inf) {
        a->cls = float_class_zero;
        return a;
    }

    /* X / 0 => Inf */
    g_assert(b->cls == float_class_zero);
    float_raise(float_flag_divbyzero, s);
    a->cls = float_class_inf;
    return a;
}

/*
 * Rounds the floating-point value `a' to an integer, and returns the
 * result as a floating-point value. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 *
 * parts_round_to_int_normal is an internal helper function for
 * normal numbers only, returning true for inexact but not directly
 * raising float_flag_inexact.
 */
static bool partsN(round_to_int_normal)(FloatPartsN *a, FloatRoundMode rmode,
                                        int scale, int frac_size)
{
    uint64_t frac_lsb, frac_lsbm1, rnd_even_mask, rnd_mask, inc;
    int shift_adj;

    scale = MIN(MAX(scale, -0x10000), 0x10000);
    a->exp += scale;

    if (a->exp < 0) {
        bool one;

        /* All fractional */
        switch (rmode) {
        case float_round_nearest_even:
            one = false;
            if (a->exp == -1) {
                FloatPartsN tmp;
                /* Shift left one, discarding DECOMPOSED_IMPLICIT_BIT */
                frac_add(&tmp, a, a);
                /* Anything remaining means frac > 0.5. */
                one = !frac_eqz(&tmp);
            }
            break;
        case float_round_ties_away:
            one = a->exp == -1;
            break;
        case float_round_to_zero:
            one = false;
            break;
        case float_round_up:
            one = !a->sign;
            break;
        case float_round_down:
            one = a->sign;
            break;
        case float_round_to_odd:
            one = true;
            break;
        default:
            g_assert_not_reached();
        }

        frac_clear(a);
        a->exp = 0;
        if (one) {
            a->frac_hi = DECOMPOSED_IMPLICIT_BIT;
        } else {
            a->cls = float_class_zero;
        }
        return true;
    }

    if (a->exp >= frac_size) {
        /* All integral */
        return false;
    }

    if (N > 64 && a->exp < N - 64) {
        /*
         * Rounding is not in the low word -- shift lsb to bit 2,
         * which leaves room for sticky and rounding bit.
         */
        shift_adj = (N - 1) - (a->exp + 2);
        frac_shrjam(a, shift_adj);
        frac_lsb = 1 << 2;
    } else {
        shift_adj = 0;
        frac_lsb = DECOMPOSED_IMPLICIT_BIT >> (a->exp & 63);
    }

    frac_lsbm1 = frac_lsb >> 1;
    rnd_mask = frac_lsb - 1;
    rnd_even_mask = rnd_mask | frac_lsb;

    if (!(a->frac_lo & rnd_mask)) {
        /* Fractional bits already clear, undo the shift above. */
        frac_shl(a, shift_adj);
        return false;
    }

    switch (rmode) {
    case float_round_nearest_even:
        inc = ((a->frac_lo & rnd_even_mask) != frac_lsbm1 ? frac_lsbm1 : 0);
        break;
    case float_round_ties_away:
        inc = frac_lsbm1;
        break;
    case float_round_to_zero:
        inc = 0;
        break;
    case float_round_up:
        inc = a->sign ? 0 : rnd_mask;
        break;
    case float_round_down:
        inc = a->sign ? rnd_mask : 0;
        break;
    case float_round_to_odd:
        inc = a->frac_lo & frac_lsb ? 0 : rnd_mask;
        break;
    default:
        g_assert_not_reached();
    }

    if (shift_adj == 0) {
        if (frac_addi(a, a, inc)) {
            frac_shr(a, 1);
            a->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
            a->exp++;
        }
        a->frac_lo &= ~rnd_mask;
    } else {
        frac_addi(a, a, inc);
        a->frac_lo &= ~rnd_mask;
        /* Be careful shifting back, not to overflow */
        frac_shl(a, shift_adj - 1);
        if (a->frac_hi & DECOMPOSED_IMPLICIT_BIT) {
            a->exp++;
        } else {
            frac_add(a, a, a);
        }
    }
    return true;
}

static void partsN(round_to_int)(FloatPartsN *a, FloatRoundMode rmode,
                                 int scale, float_status *s,
                                 const FloatFmt *fmt)
{
    switch (a->cls) {
    case float_class_qnan:
    case float_class_snan:
        parts_return_nan(a, s);
        break;
    case float_class_zero:
    case float_class_inf:
        break;
    case float_class_normal:
        if (parts_round_to_int_normal(a, rmode, scale, fmt->frac_size)) {
            float_raise(float_flag_inexact, s);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Returns the result of converting the floating-point value `a' to
 * the two's complement integer format. The conversion is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic---which means in particular that the conversion is
 * rounded according to the current rounding mode. If `a' is a NaN,
 * the largest positive integer is returned. Otherwise, if the
 * conversion overflows, the largest integer with the same sign as `a'
 * is returned.
 */
static int64_t partsN(float_to_sint)(FloatPartsN *p, FloatRoundMode rmode,
                                     int scale, int64_t min, int64_t max,
                                     float_status *s)
{
    int flags = 0;
    uint64_t r;

    switch (p->cls) {
    case float_class_snan:
    case float_class_qnan:
        flags = float_flag_invalid;
        r = max;
        break;

    case float_class_inf:
        flags = float_flag_invalid;
        r = p->sign ? min : max;
        break;

    case float_class_zero:
        return 0;

    case float_class_normal:
        /* TODO: N - 2 is frac_size for rounding; could use input fmt. */
        if (parts_round_to_int_normal(p, rmode, scale, N - 2)) {
            flags = float_flag_inexact;
        }

        if (p->exp <= DECOMPOSED_BINARY_POINT) {
            r = p->frac_hi >> (DECOMPOSED_BINARY_POINT - p->exp);
        } else {
            r = UINT64_MAX;
        }
        if (p->sign) {
            if (r <= -(uint64_t)min) {
                r = -r;
            } else {
                flags = float_flag_invalid;
                r = min;
            }
        } else if (r > max) {
            flags = float_flag_invalid;
            r = max;
        }
        break;

    default:
        g_assert_not_reached();
    }

    float_raise(flags, s);
    return r;
}

/*
 *  Returns the result of converting the floating-point value `a' to
 *  the unsigned integer format. The conversion is performed according
 *  to the IEC/IEEE Standard for Binary Floating-Point
 *  Arithmetic---which means in particular that the conversion is
 *  rounded according to the current rounding mode. If `a' is a NaN,
 *  the largest unsigned integer is returned. Otherwise, if the
 *  conversion overflows, the largest unsigned integer is returned. If
 *  the 'a' is negative, the result is rounded and zero is returned;
 *  values that do not round to zero will raise the inexact exception
 *  flag.
 */
static uint64_t partsN(float_to_uint)(FloatPartsN *p, FloatRoundMode rmode,
                                      int scale, uint64_t max, float_status *s)
{
    int flags = 0;
    uint64_t r;

    switch (p->cls) {
    case float_class_snan:
    case float_class_qnan:
        flags = float_flag_invalid;
        r = max;
        break;

    case float_class_inf:
        flags = float_flag_invalid;
        r = p->sign ? 0 : max;
        break;

    case float_class_zero:
        return 0;

    case float_class_normal:
        /* TODO: N - 2 is frac_size for rounding; could use input fmt. */
        if (parts_round_to_int_normal(p, rmode, scale, N - 2)) {
            flags = float_flag_inexact;
            if (p->cls == float_class_zero) {
                r = 0;
                break;
            }
        }

        if (p->sign) {
            flags = float_flag_invalid;
            r = 0;
        } else if (p->exp > DECOMPOSED_BINARY_POINT) {
            flags = float_flag_invalid;
            r = max;
        } else {
            r = p->frac_hi >> (DECOMPOSED_BINARY_POINT - p->exp);
            if (r > max) {
                flags = float_flag_invalid;
                r = max;
            }
        }
        break;

    default:
        g_assert_not_reached();
    }

    float_raise(flags, s);
    return r;
}

/*
 * Integer to float conversions
 *
 * Returns the result of converting the two's complement integer `a'
 * to the floating-point format. The conversion is performed according
 * to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */
static void partsN(sint_to_float)(FloatPartsN *p, int64_t a,
                                  int scale, float_status *s)
{
    uint64_t f = a;
    int shift;

    memset(p, 0, sizeof(*p));

    if (a == 0) {
        p->cls = float_class_zero;
        return;
    }

    p->cls = float_class_normal;
    if (a < 0) {
        f = -f;
        p->sign = true;
    }
    shift = clz64(f);
    scale = MIN(MAX(scale, -0x10000), 0x10000);

    p->exp = DECOMPOSED_BINARY_POINT - shift + scale;
    p->frac_hi = f << shift;
}

/*
 * Unsigned Integer to float conversions
 *
 * Returns the result of converting the unsigned integer `a' to the
 * floating-point format. The conversion is performed according to the
 * IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */
static void partsN(uint_to_float)(FloatPartsN *p, uint64_t a,
                                  int scale, float_status *status)
{
    memset(p, 0, sizeof(*p));

    if (a == 0) {
        p->cls = float_class_zero;
    } else {
        int shift = clz64(a);
        scale = MIN(MAX(scale, -0x10000), 0x10000);
        p->cls = float_class_normal;
        p->exp = DECOMPOSED_BINARY_POINT - shift + scale;
        p->frac_hi = a << shift;
    }
}
