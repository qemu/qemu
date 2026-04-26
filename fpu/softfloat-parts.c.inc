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

FloatPartsN partsN(return_nan)(const FloatPartsN *a, float_status *s)
{
    switch (a->cls) {
    case float_class_snan:
        float_raise(float_flag_invalid | float_flag_invalid_snan, s);
        if (s->default_nan_mode) {
            return partsN(default_nan)(s);
        } else {
            return partsN(silence_nan)(a, s);
        }
        break;
    case float_class_qnan:
        if (s->default_nan_mode) {
            return partsN(default_nan)(s);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return *a;
}

FloatPartsN partsN(pick_nan)(const FloatPartsN *a, const FloatPartsN *b,
                             float_status *s)
{
    bool have_snan = false;
    const FloatPartsN *ret;
    int cmp;

    if (is_snan(a->cls) || is_snan(b->cls)) {
        float_raise(float_flag_invalid | float_flag_invalid_snan, s);
        have_snan = true;
    }

    if (s->default_nan_mode) {
        return partsN(default_nan)(s);
    }

    switch (s->float_2nan_prop_rule) {
    case float_2nan_prop_s_ab:
        if (have_snan) {
            ret = is_snan(a->cls) ? a : b;
            break;
        }
        /* fall through */
    case float_2nan_prop_ab:
        ret = is_nan(a->cls) ? a : b;
        break;
    case float_2nan_prop_s_ba:
        if (have_snan) {
            ret = is_snan(b->cls) ? b : a;
            break;
        }
        /* fall through */
    case float_2nan_prop_ba:
        ret = is_nan(b->cls) ? b : a;
        break;
    case float_2nan_prop_x87:
        /*
         * This implements x87 NaN propagation rules:
         * SNaN + QNaN => return the QNaN
         * two SNaNs => return the one with the larger significand, silenced
         * two QNaNs => return the one with the larger significand
         * SNaN and a non-NaN => return the SNaN, silenced
         * QNaN and a non-NaN => return the QNaN
         *
         * If we get down to comparing significands and they are the same,
         * return the NaN with the positive sign bit (if any).
         */
        if (is_snan(a->cls)) {
            if (!is_snan(b->cls)) {
                ret = is_qnan(b->cls) ? b : a;
                break;
            }
        } else if (is_qnan(a->cls)) {
            if (is_snan(b->cls) || !is_qnan(b->cls)) {
                ret = a;
                break;
            }
        } else {
            ret = b;
            break;
        }
        cmp = fracN(cmp)(a, b);
        if (cmp == 0) {
            cmp = a->sign < b->sign;
        }
        ret = cmp > 0 ? a : b;
        break;
    default:
        g_assert_not_reached();
    }

    if (is_snan(ret->cls)) {
        return partsN(silence_nan)(ret, s);
    }
    return *ret;
}

static FloatPartsN partsN(pick_nan_muladd)(const FloatPartsN *a,
                                           const FloatPartsN *b,
                                           const FloatPartsN *c,
                                           float_status *s,
                                           int ab_mask, int abc_mask)
{
    bool infzero = (ab_mask == float_cmask_infzero);
    bool have_snan = (abc_mask & float_cmask_snan);
    const FloatPartsN *ret;

    if (unlikely(have_snan)) {
        float_raise(float_flag_invalid | float_flag_invalid_snan, s);
    }

    if (infzero &&
        !(s->float_infzeronan_rule & float_infzeronan_suppress_invalid)) {
        /* This is (0 * inf) + NaN or (inf * 0) + NaN */
        float_raise(float_flag_invalid | float_flag_invalid_imz, s);
    }

    if (s->default_nan_mode) {
        /*
         * We guarantee not to require the target to tell us how to
         * pick a NaN if we're always returning the default NaN.
         * But if we're not in default-NaN mode then the target must
         * specify.
         */
        goto default_nan;
    } else if (infzero) {
        /*
         * Inf * 0 + NaN -- some implementations return the
         * default NaN here, and some return the input NaN.
         */
        switch (s->float_infzeronan_rule & ~float_infzeronan_suppress_invalid) {
        case float_infzeronan_dnan_never:
            break;
        case float_infzeronan_dnan_always:
            goto default_nan;
        case float_infzeronan_dnan_if_qnan:
            if (is_qnan(c->cls)) {
                goto default_nan;
            }
            break;
        default:
            g_assert_not_reached();
        }
        ret = c;
    } else {
        const FloatPartsN *val[R_3NAN_1ST_MASK + 1] = { a, b, c };
        Float3NaNPropRule rule = s->float_3nan_prop_rule;

        assert(rule != float_3nan_prop_none);
        if (have_snan && (rule & R_3NAN_SNAN_MASK)) {
            /* We have at least one SNaN input and should prefer it */
            do {
                ret = val[rule & R_3NAN_1ST_MASK];
                rule >>= R_3NAN_1ST_LENGTH;
            } while (!is_snan(ret->cls));
        } else {
            do {
                ret = val[rule & R_3NAN_1ST_MASK];
                rule >>= R_3NAN_1ST_LENGTH;
            } while (!is_nan(ret->cls));
        }
    }

    if (is_snan(ret->cls)) {
        return partsN(silence_nan)(ret, s);
    }
    return *ret;

 default_nan:
    return partsN(default_nan)(s);
}

/*
 * Canonicalize the FloatParts structure.  Determine the class,
 * unbias the exponent, and normalize the fraction.
 */
static void partsN(canonicalize)(FloatPartsN *p, float_status *status,
                                 const FloatFmt *fmt)
{
    /*
     * It's target-dependent how to handle the case of exponent 0
     * and Integer bit set. Intel calls these "pseudodenormals",
     * and treats them as if the integer bit was 0, and never
     * produces them on output. This is the default behaviour for QEMU.
     * For m68k, the integer bit is considered validly part of the
     * input value when the exponent is 0, and may be 0 or 1,
     * giving extra range. They may also be generated as outputs.
     * (The m68k manual actually calls these values part of the
     * normalized number range, not the denormalized number range,
     * but that distinction is not important for us, because
     * m68k doesn't care about the input_denormal_used status flag.)
     * floatx80_pseudo_denormal_valid selects the m68k behaviour,
     * which changes both how we canonicalize such a value and
     * how we uncanonicalize results.
     */
    bool has_pseudo_denormals = fmt->has_explicit_bit &&
        (status->floatx80_behaviour & floatx80_pseudo_denormal_valid);

    if (unlikely(p->exp == 0)) {
        if (likely(fracN(eqz)(p))) {
            p->cls = float_class_zero;
        } else if (status->flush_inputs_to_zero) {
            float_raise(float_flag_input_denormal_flushed, status);
            p->cls = float_class_zero;
            fracN(clear)(p);
        } else {
            int shift = fracN(normalize)(p);
            p->cls = float_class_denormal;
            p->exp = fmt->frac_shift - fmt->exp_bias
                   - shift + !has_pseudo_denormals;
        }
        return;
    }
    if (unlikely(p->exp == fmt->exp_max)) {
        switch (fmt->exp_max_kind) {
        case float_expmax_ieee:
            if (likely(fracN(eqz)(p))) {
                p->cls = float_class_inf;
            } else {
                fracN(shl)(p, fmt->frac_shift);
                p->cls = (parts_is_snan_frac(p->frac_hi, status)
                          ? float_class_snan : float_class_qnan);
            }
            return;
        case float_expmax_normal:
            break;
        case float_expmax_e4m3:
            if (p->frac_hi == 0b111) {
                fracN(shl)(p, fmt->frac_shift);
                p->cls = (parts_is_snan_frac(p->frac_hi, status)
                          ? float_class_snan : float_class_qnan);
                return;
            }
            /* otherwise normal */
            break;
        default:
            g_assert_not_reached();
        }
    }

    p->cls = float_class_normal;
    p->exp -= fmt->exp_bias;
    fracN(shl)(p, fmt->frac_shift);
    p->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
}

/*
 * Round and uncanonicalize a floating-point number by parts. There
 * are FRAC_SHIFT bits that may require rounding at the bottom of the
 * fraction; these bits will be removed. The exponent will be biased
 * by EXP_BIAS and must be bounded by [EXP_MAX-1, 0].
 *
 * The saturate parameter controls saturation behavior for formats that
 * support it -- when true, overflow produces max normal instead of infinity.
 */

/* Helper for uncanon_normal and uncanon, for FP8 E4M3. */
static void partsN(uncanon_e4m3_overflow)(FloatPartsN *p, float_status *s,
                                          const FloatFmt *fmt, bool saturate)
{
    assert(N == 64);
    if (saturate) {
        p->exp = fmt->exp_max;
        p->frac_hi = E4M3_NORMAL_FRAC_MAX;
    } else {
        *p = partsN(default_nan)(s);
    }
}

static void partsN(uncanon_normal)(FloatPartsN *p, float_status *s,
                                   const FloatFmt *fmt, bool saturate)
{
    const int exp_max = fmt->exp_max;
    const int frac_shift = fmt->frac_shift;
    const uint64_t round_mask = fmt->round_mask;
    const uint64_t frac_lsb = round_mask + 1;
    const uint64_t frac_lsbm1 = round_mask ^ (round_mask >> 1);
    const uint64_t roundeven_mask = round_mask | frac_lsb;
    uint64_t inc;
    bool overflow_norm = saturate;
    int exp, flags = 0;

    switch (s->float_rounding_mode) {
    case float_round_nearest_even_max:
        overflow_norm = true;
        /* fall through */
    case float_round_nearest_even:
        if (N > 64 && frac_lsb == 0) {
            inc = ((p->frac_hi & 1) || (p->frac_lo & round_mask) != frac_lsbm1
                   ? frac_lsbm1 : 0);
        } else {
            inc = ((p->frac_lo & roundeven_mask) != frac_lsbm1
                   ? frac_lsbm1 : 0);
        }
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
        overflow_norm |= p->sign;
        break;
    case float_round_down:
        inc = p->sign ? round_mask : 0;
        overflow_norm |= !p->sign;
        break;
    case float_round_to_odd:
        overflow_norm = true;
        /* fall through */
    case float_round_to_odd_inf:
        if (N > 64 && frac_lsb == 0) {
            inc = p->frac_hi & 1 ? 0 : round_mask;
        } else {
            inc = p->frac_lo & frac_lsb ? 0 : round_mask;
        }
        break;
    default:
        g_assert_not_reached();
    }

    exp = p->exp + fmt->exp_bias;
    if (likely(exp > 0)) {
        if (p->frac_lo & round_mask) {
            flags |= float_flag_inexact;
            if (fracN(addi)(p, p, inc)) {
                fracN(shr)(p, 1);
                p->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
                exp++;
            }
            p->frac_lo &= ~round_mask;
        }

        if (unlikely(exp >= exp_max)) {
            switch (fmt->exp_max_kind) {
            case float_expmax_ieee:
                flags |= float_flag_overflow;
                if (s->rebias_overflow) {
                    exp -= fmt->exp_re_bias;
                } else if (overflow_norm) {
                    flags |= float_flag_inexact;
                    exp = exp_max - 1;
                    fracN(allones)(p);
                    p->frac_lo &= ~round_mask;
                } else {
                    flags |= float_flag_inexact;
                    p->cls = float_class_inf;
                    exp = exp_max;
                    fracN(clear)(p);
                }
                break;

            case float_expmax_normal:
                if (unlikely(exp > exp_max)) {
                    /* Overflow.  Return the maximum normal.  */
                    flags = (fmt->overflow_raises_invalid
                             ? float_flag_invalid
                             : float_flag_overflow | float_flag_inexact);
                    exp = exp_max;
                    fracN(allones)(p);
                    p->frac_lo &= ~round_mask;
                }
                break;

            case float_expmax_e4m3:
                if (exp > exp_max || p->frac_hi > E4M3_NORMAL_FRAC_MAX) {
                    partsN(uncanon_e4m3_overflow)(p, s, fmt, overflow_norm);
                    exp = p->exp;
                    flags |= (float_flag_overflow | float_flag_inexact);
                }
                break;

            default:
                g_assert_not_reached();
            }
        }
        fracN(shr)(p, frac_shift);
    } else if (unlikely(s->rebias_underflow)) {
        flags |= float_flag_underflow;
        exp += fmt->exp_re_bias;
        if (p->frac_lo & round_mask) {
            flags |= float_flag_inexact;
            if (fracN(addi)(p, p, inc)) {
                fracN(shr)(p, 1);
                p->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
                exp++;
            }
            p->frac_lo &= ~round_mask;
        }
        fracN(shr)(p, frac_shift);
    } else if (s->flush_to_zero &&
               s->ftz_detection == float_ftz_before_rounding) {
        flags |= float_flag_output_denormal_flushed;
        p->cls = float_class_zero;
        exp = 0;
        fracN(clear)(p);
    } else {
        bool is_tiny = s->tininess_before_rounding || exp < 0;
        bool has_pseudo_denormals = fmt->has_explicit_bit &&
            (s->floatx80_behaviour & floatx80_pseudo_denormal_valid);

        if (!is_tiny) {
            FloatPartsN discard;
            is_tiny = !fracN(addi)(&discard, p, inc);
        }

        fracN(shrjam)(p, !has_pseudo_denormals - exp);

        if (p->frac_lo & round_mask) {
            /* Need to recompute round-to-even/round-to-odd. */
            switch (s->float_rounding_mode) {
            case float_round_nearest_even:
                if (N > 64 && frac_lsb == 0) {
                    inc = ((p->frac_hi & 1) ||
                           (p->frac_lo & round_mask) != frac_lsbm1
                           ? frac_lsbm1 : 0);
                } else {
                    inc = ((p->frac_lo & roundeven_mask) != frac_lsbm1
                           ? frac_lsbm1 : 0);
                }
                break;
            case float_round_to_odd:
            case float_round_to_odd_inf:
                if (N > 64 && frac_lsb == 0) {
                    inc = p->frac_hi & 1 ? 0 : round_mask;
                } else {
                    inc = p->frac_lo & frac_lsb ? 0 : round_mask;
                }
                break;
            default:
                break;
            }
            flags |= float_flag_inexact;
            fracN(addi)(p, p, inc);
            p->frac_lo &= ~round_mask;
        }

        exp = (p->frac_hi & DECOMPOSED_IMPLICIT_BIT) && !has_pseudo_denormals;
        fracN(shr)(p, frac_shift);

        if (is_tiny) {
            if (s->flush_to_zero) {
                assert(s->ftz_detection == float_ftz_after_rounding);
                flags |= float_flag_output_denormal_flushed;
                p->cls = float_class_zero;
                exp = 0;
                fracN(clear)(p);
            } else if (flags & float_flag_inexact) {
                flags |= float_flag_underflow;
            }
            if (exp == 0 && fracN(eqz)(p)) {
                p->cls = float_class_zero;
            }
        }
    }
    p->exp = exp;
    float_raise(flags, s);
}

static void partsN(uncanon)(FloatPartsN *p, float_status *s,
                            const FloatFmt *fmt, bool saturate)
{
    if (likely(is_anynorm(p->cls))) {
        partsN(uncanon_normal)(p, s, fmt, saturate);
    } else {
        switch (p->cls) {
        case float_class_zero:
            p->exp = 0;
            fracN(clear)(p);
            return;
        case float_class_inf:
            switch (fmt->exp_max_kind) {
            case float_expmax_ieee:
                p->exp = fmt->exp_max;
                fracN(clear)(p);
                break;
            case float_expmax_e4m3:
                partsN(uncanon_e4m3_overflow)(p, s, fmt, saturate);
                fracN(shr)(p, fmt->frac_shift);
                break;
            case float_expmax_normal:
            default:
                g_assert_not_reached();
            }
            return;
        case float_class_qnan:
        case float_class_snan:
            assert(fmt->exp_max_kind != float_expmax_normal);
            p->exp = fmt->exp_max;
            fracN(shr)(p, fmt->frac_shift);
            return;
        default:
            break;
        }
        g_assert_not_reached();
    }
}

/*
 * Returns the result of adding or subtracting the values of the
 * floating-point values `a' and `b'. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 */
FloatPartsN partsN(addsub)(const FloatPartsN *a_orig,
                           const FloatPartsN *b_orig,
                           float_status *s, bool subtract)
{
    int ab_mask = float_cmask(a_orig->cls) | float_cmask(b_orig->cls);

    if (unlikely(ab_mask & float_cmask_anynan)) {
        return partsN(pick_nan)(a_orig, b_orig, s);
    }

    /*
     * For addition and subtraction, we will consume an
     * input denormal unless the other input is a NaN.
     */
    record_denormals_used(ab_mask, s);

    FloatPartsN a = *a_orig;
    FloatPartsN b = *b_orig;

    b.sign ^= subtract;

    if (a.sign != b.sign) {
        /* Subtraction */
        if (likely(cmask_is_only_normals(ab_mask))) {
            if (partsN(sub_normal)(&a, &b)) {
                return a;
            }
            /* Subtract was exact, fall through to set sign. */
            ab_mask = float_cmask_zero;
        }

        if (ab_mask == float_cmask_zero) {
            /* 0 - 0 */
            a.sign = s->float_rounding_mode == float_round_down;
            return a;
        }

        if (ab_mask & float_cmask_inf) {
            if (a.cls != float_class_inf) {
                /* N - Inf */
                return b;
            }
            if (b.cls != float_class_inf) {
                /* Inf - N */
                return a;
            }
            /* Inf - Inf */
            float_raise(float_flag_invalid | float_flag_invalid_isi, s);
            return partsN(default_nan)(s);
        }
    } else {
        /* Addition */
        if (likely(cmask_is_only_normals(ab_mask))) {
            partsN(add_normal)(&a, &b);
            return a;
        }

        if (ab_mask == float_cmask_zero) {
            /* 0 + 0 */
            return a;
        }

        if (ab_mask & float_cmask_inf) {
            /* N + Inf or Inf + N */
            a.cls = float_class_inf;
            return a;
        }
    }

    /* 0 +/- N or N +/- 0 */
    assert((ab_mask & float_cmask_zero) && (ab_mask & float_cmask_anynorm));
    return b.cls == float_class_zero ? a : b;
}

/*
 * Returns the result of multiplying the floating-point values `a' and
 * `b'. The operation is performed according to the IEC/IEEE Standard
 * for Binary Floating-Point Arithmetic.
 */
FloatPartsN partsN(mul)(const FloatPartsN *a, const FloatPartsN *b,
                        float_status *s)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    bool sign = a->sign ^ b->sign;

    if (likely(cmask_is_only_normals(ab_mask))) {
        FloatPartsW tmp;
        FloatPartsN r = {
            .cls = float_class_normal,
            .sign = sign,
            .exp = a->exp + b->exp + 1,
        };

        record_denormals_used(ab_mask, s);

        fracN(mulw)(&tmp, a, b);
        fracN(truncjam)(&r, &tmp);

        if (!(r.frac_hi & DECOMPOSED_IMPLICIT_BIT)) {
            fracN(add)(&r, &r, &r);
            r.exp -= 1;
        }

        return r;
    }

    /* Inf * Zero == NaN */
    if (unlikely(ab_mask == float_cmask_infzero)) {
        float_raise(float_flag_invalid | float_flag_invalid_imz, s);
        return partsN(default_nan)(s);
    }

    if (unlikely(ab_mask & float_cmask_anynan)) {
        return partsN(pick_nan)(a, b, s);
    }

    /* Multiply by 0 or Inf */
    record_denormals_used(ab_mask, s);

    if (ab_mask & float_cmask_inf) {
        return (FloatPartsN){ .cls = float_class_inf, .sign = sign };
    }

    g_assert(ab_mask & float_cmask_zero);
    return (FloatPartsN){ .cls = float_class_zero, .sign = sign };
}

/*
 * Returns the result of multiplying the floating-point values `a' and
 * `b' then adding 'c', with no intermediate rounding step after the
 * multiplication. The operation is performed according to the
 * IEC/IEEE Standard for Binary Floating-Point Arithmetic 754-2008.
 * The flags argument allows the caller to select negation of the addend
 * or the intermediate product. (The difference between this and having
 * the caller do a separate negation is that negating externally will
 * flip the sign bit on NaNs.) Note that float_muladd_negate_result
 * is not applied here, and should be handled separately after rounding
 * chooses the final sign of 0.0.
 *
 * Requires A and C extracted into a double-sized structure to provide the
 * extra space for the widening multiply.
 */
FloatPartsN partsN(muladd)(const FloatPartsN *a, const FloatPartsN *b,
                           const FloatPartsN *c, int flags, float_status *s)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    int c_mask = float_cmask(c->cls);
    int abc_mask = ab_mask | c_mask;
    bool c_sign = c->sign ^ !!(flags & float_muladd_negate_c);
    bool p_sign = a->sign ^ b->sign ^ !!(flags & float_muladd_negate_product);

    /*
     * The "likely" case is A and B normal, so that the product is normal,
     * and C normal or zero so that the result is normal.
     */
    int likely_mask = ab_mask | (c_mask & ~float_cmask_zero);
    if (likely(cmask_is_only_normals(likely_mask))) {
        record_denormals_used(abc_mask, s);

        /* Perform the multiplication step. */
        FloatPartsW p_widen = { .sign = p_sign, .exp = a->exp + b->exp + 1 };
        fracN(mulw)(&p_widen, a, b);
        if (!(p_widen.frac_hi & DECOMPOSED_IMPLICIT_BIT)) {
            fracW(add)(&p_widen, &p_widen, &p_widen);
            p_widen.exp -= 1;
        }

        /* Perform the addition step. */
        if (!(c_mask & float_cmask_zero)) {
            /* Zero-extend C to less significant bits. */
            FloatPartsW c_widen = { .sign = c_sign, .exp = c->exp };
            fracN(widen)(&c_widen, c);

            if (p_sign == c_sign) {
                partsW(add_normal)(&p_widen, &c_widen);
            } else if (!partsW(sub_normal)(&p_widen, &c_widen)) {
                goto return_sub_zero;
            }
        }

        /* Narrow with sticky bit, for proper rounding later. */
        FloatPartsN r = {
            .sign = p_widen.sign,
            .exp = p_widen.exp,
            .cls = float_class_normal,
        };
        fracN(truncjam)(&r, &p_widen);
        return r;
    }

    /*
     * It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (unlikely(abc_mask & float_cmask_anynan)) {
        return partsN(pick_nan_muladd)(a, b, c, s, ab_mask, abc_mask);
    }

    if (unlikely(ab_mask == float_cmask_infzero)) {
        /* Inf * Zero == NaN */
        float_raise(float_flag_invalid | float_flag_invalid_imz, s);
        goto d_nan;
    }

    if (unlikely(ab_mask & float_cmask_inf)) {
        if ((c_mask & float_cmask_inf) && p_sign != c_sign) {
            /* Inf - Inf == NaN */
            float_raise(float_flag_invalid | float_flag_invalid_isi, s);
            goto d_nan;
        }
        /* Inf + C == Inf */
        record_denormals_used(abc_mask, s);
        return (FloatPartsN){ .sign = p_sign, .cls = float_class_inf };
    }
    record_denormals_used(abc_mask, s);

    /* Only remaining cases are zero product or inf addend. */
    assert((ab_mask & float_cmask_zero) | (c_mask & float_cmask_inf));

    /*
     * P + Inf == Inf, or
     * 0 + C == C,
     * except for 0 - 0, which needs special rounding,
     * except for when we want to suppress this addition step.
     */
    if (!(c_mask & float_cmask_zero)
        || p_sign == c_sign
        || (flags & float_muladd_suppress_add_product_zero)) {
        FloatPartsN r = *c;
        r.sign = c_sign;
        return r;
    }

 return_sub_zero:
    /* 0 - 0 == -0 for round_down, +0 otherwise. */
    return (FloatPartsN){
        .sign = s->float_rounding_mode == float_round_down,
        .cls = float_class_zero
    };

 d_nan:
    return partsN(default_nan)(s);
}

/*
 * Returns the result of dividing the floating-point value `a' by the
 * corresponding value `b'. The operation is performed according to
 * the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
 */
FloatPartsN partsN(div)(const FloatPartsN *a, const FloatPartsN *b,
                        float_status *s)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    FloatPartsN r = *a;

    r.sign ^= b->sign;
    r.exp -= b->exp;

    if (likely(cmask_is_only_normals(ab_mask))) {
        record_denormals_used(ab_mask, s);
        r.exp -= fracN(div)(&r, b);
        return r;
    }

    /* 0/0 or Inf/Inf => NaN */
    if (unlikely(ab_mask == float_cmask_zero)) {
        float_raise(float_flag_invalid | float_flag_invalid_zdz, s);
        return partsN(default_nan)(s);
    }
    if (unlikely(ab_mask == float_cmask_inf)) {
        float_raise(float_flag_invalid | float_flag_invalid_idi, s);
        return partsN(default_nan)(s);
    }

    /* All the NaN cases */
    if (unlikely(ab_mask & float_cmask_anynan)) {
        return partsN(pick_nan)(a, b, s);
    }

    if (b->cls != float_class_zero) {
        record_denormals_used(ab_mask, s);
    }

    /* Inf / X */
    if (r.cls == float_class_inf) {
        return r;
    }

    /* 0 / X */
    if (r.cls == float_class_zero) {
        return r;
    }

    /* X / Inf */
    if (b->cls == float_class_inf) {
        r.cls = float_class_zero;
        return r;
    }

    /* X / 0 => Inf */
    assert(b->cls == float_class_zero);
    float_raise(float_flag_divbyzero, s);
    r.cls = float_class_inf;
    return r;
}

/*
 * Floating point remainder, per IEC/IEEE, or modulus.
 */
static FloatPartsN *partsN(modrem)(FloatPartsN *a, FloatPartsN *b,
                                   uint64_t *mod_quot, float_status *s)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);

    if (likely(cmask_is_only_normals(ab_mask))) {
        record_denormals_used(ab_mask, s);
        fracN(modrem)(a, b, mod_quot);
        return a;
    }

    if (mod_quot) {
        *mod_quot = 0;
    }

    /* All the NaN cases */
    if (unlikely(ab_mask & float_cmask_anynan)) {
        *a = partsN(pick_nan)(a, b, s);
        return a;
    }

    /* Inf % N; N % 0 */
    if (a->cls == float_class_inf || b->cls == float_class_zero) {
        float_raise(float_flag_invalid, s);
        *a = partsN(default_nan)(s);
        return a;
    }

    record_denormals_used(ab_mask, s);

    /* N % Inf; 0 % N */
    g_assert(b->cls == float_class_inf || a->cls == float_class_zero);
    return a;
}

/*
 * Square Root
 *
 * The base algorithm is lifted from
 * https://git.musl-libc.org/cgit/musl/tree/src/math/sqrtf.c
 * https://git.musl-libc.org/cgit/musl/tree/src/math/sqrt.c
 * https://git.musl-libc.org/cgit/musl/tree/src/math/sqrtl.c
 * and is thus MIT licenced.
 */
static void partsN(sqrt)(FloatPartsN *a, float_status *status,
                         const FloatFmt *fmt)
{
    const uint32_t three32 = 3u << 30;
    const uint64_t three64 = 3ull << 62;
    uint32_t d32, m32, r32, s32, u32;            /* 32-bit computation */
    uint64_t d64, m64, r64, s64, u64;            /* 64-bit computation */
    uint64_t dh, dl, rh, rl, sh, sl, uh, ul;     /* 128-bit computation */
    uint64_t d0h, d0l, d1h, d1l, d2h, d2l;
    uint64_t discard;
    bool exp_odd;
    size_t index;

    if (unlikely(a->cls != float_class_normal)) {
        switch (a->cls) {
        case float_class_denormal:
            if (!a->sign) {
                /* -ve denormal will be InvalidOperation */
                float_raise(float_flag_input_denormal_used, status);
            }
            break;
        case float_class_snan:
        case float_class_qnan:
            *a = partsN(return_nan)(a, status);
            return;
        case float_class_zero:
            return;
        case float_class_inf:
            if (unlikely(a->sign)) {
                goto d_nan;
            }
            return;
        default:
            g_assert_not_reached();
        }
    }

    if (unlikely(a->sign)) {
        goto d_nan;
    }

    /*
     * Argument reduction.
     * x = 4^e frac; with integer e, and frac in [1, 4)
     * m = frac fixed point at bit 62, since we're in base 4.
     * If base-2 exponent is odd, exchange that for multiply by 2,
     * which results in no shift.
     */
    exp_odd = a->exp & 1;
    index = extract64(a->frac_hi, 57, 6) | (!exp_odd << 6);
    if (!exp_odd) {
        fracN(shr)(a, 1);
    }

    /*
     * Approximate r ~= 1/sqrt(m) and s ~= sqrt(m) when m in [1, 4).
     *
     * Initial estimate:
     * 7-bit lookup table (1-bit exponent and 6-bit significand).
     *
     * The relative error (e = r0*sqrt(m)-1) of a linear estimate
     * (r0 = a*m + b) is |e| < 0.085955 ~ 0x1.6p-4 at best;
     * a table lookup is faster and needs one less iteration.
     * The 7-bit table gives |e| < 0x1.fdp-9.
     *
     * A Newton-Raphson iteration for r is
     *   s = m*r
     *   d = s*r
     *   u = 3 - d
     *   r = r*u/2
     *
     * Fixed point representations:
     *   m, s, d, u, three are all 2.30; r is 0.32
     */
    m64 = a->frac_hi;
    m32 = m64 >> 32;

    r32 = rsqrt_tab[index] << 16;
    /* |r*sqrt(m) - 1| < 0x1.FDp-9 */

    s32 = ((uint64_t)m32 * r32) >> 32;
    d32 = ((uint64_t)s32 * r32) >> 32;
    u32 = three32 - d32;

    if (N == 64) {
        /* float64 or smaller */

        r32 = ((uint64_t)r32 * u32) >> 31;
        /* |r*sqrt(m) - 1| < 0x1.7Bp-16 */

        s32 = ((uint64_t)m32 * r32) >> 32;
        d32 = ((uint64_t)s32 * r32) >> 32;
        u32 = three32 - d32;

        if (fmt->frac_size <= 23) {
            /* float32 or smaller */

            s32 = ((uint64_t)s32 * u32) >> 32;  /* 3.29 */
            s32 = (s32 - 1) >> 6;               /* 9.23 */
            /* s < sqrt(m) < s + 0x1.08p-23 */

            /* compute nearest rounded result to 2.23 bits */
            uint32_t d0 = (m32 << 16) - s32 * s32;
            uint32_t d1 = s32 - d0;
            uint32_t d2 = d1 + s32 + 1;
            s32 += d1 >> 31;
            a->frac_hi = (uint64_t)s32 << (64 - 25);

            /* increment or decrement for inexact */
            if (d2 != 0) {
                a->frac_hi += ((int32_t)(d1 ^ d2) < 0 ? -1 : 1);
            }
            goto done;
        }

        /* float64 */

        r64 = (uint64_t)r32 * u32 * 2;
        /* |r*sqrt(m) - 1| < 0x1.37-p29; convert to 64-bit arithmetic */
        mul64To128(m64, r64, &s64, &discard);
        mul64To128(s64, r64, &d64, &discard);
        u64 = three64 - d64;

        mul64To128(s64, u64, &s64, &discard);  /* 3.61 */
        s64 = (s64 - 2) >> 9;                  /* 12.52 */

        /* Compute nearest rounded result */
        uint64_t d0 = (m64 << 42) - s64 * s64;
        uint64_t d1 = s64 - d0;
        uint64_t d2 = d1 + s64 + 1;
        s64 += d1 >> 63;
        a->frac_hi = s64 << (64 - 54);

        /* increment or decrement for inexact */
        if (d2 != 0) {
            a->frac_hi += ((int64_t)(d1 ^ d2) < 0 ? -1 : 1);
        }
        goto done;
    }

    r64 = (uint64_t)r32 * u32 * 2;
    /* |r*sqrt(m) - 1| < 0x1.7Bp-16; convert to 64-bit arithmetic */

    mul64To128(m64, r64, &s64, &discard);
    mul64To128(s64, r64, &d64, &discard);
    u64 = three64 - d64;
    mul64To128(u64, r64, &r64, &discard);
    r64 <<= 1;
    /* |r*sqrt(m) - 1| < 0x1.a5p-31 */

    mul64To128(m64, r64, &s64, &discard);
    mul64To128(s64, r64, &d64, &discard);
    u64 = three64 - d64;
    mul64To128(u64, r64, &rh, &rl);
    add128(rh, rl, rh, rl, &rh, &rl);
    /* |r*sqrt(m) - 1| < 0x1.c001p-59; change to 128-bit arithmetic */

    mul128To256(a->frac_hi, a->frac_lo, rh, rl, &sh, &sl, &discard, &discard);
    mul128To256(sh, sl, rh, rl, &dh, &dl, &discard, &discard);
    sub128(three64, 0, dh, dl, &uh, &ul);
    mul128To256(uh, ul, sh, sl, &sh, &sl, &discard, &discard);  /* 3.125 */
    /* -0x1p-116 < s - sqrt(m) < 0x3.8001p-125 */

    sub128(sh, sl, 0, 4, &sh, &sl);
    shift128Right(sh, sl, 13, &sh, &sl);  /* 16.112 */
    /* s < sqrt(m) < s + 1ulp */

    /* Compute nearest rounded result */
    mul64To128(sl, sl, &d0h, &d0l);
    d0h += 2 * sh * sl;
    sub128(a->frac_lo << 34, 0, d0h, d0l, &d0h, &d0l);
    sub128(sh, sl, d0h, d0l, &d1h, &d1l);
    add128(sh, sl, 0, 1, &d2h, &d2l);
    add128(d2h, d2l, d1h, d1l, &d2h, &d2l);
    add128(sh, sl, 0, d1h >> 63, &sh, &sl);
    shift128Left(sh, sl, 128 - 114, &sh, &sl);

    /* increment or decrement for inexact */
    if (d2h | d2l) {
        if ((int64_t)(d1h ^ d2h) < 0) {
            sub128(sh, sl, 0, 1, &sh, &sl);
        } else {
            add128(sh, sl, 0, 1, &sh, &sl);
        }
    }
    a->frac_lo = sl;
    a->frac_hi = sh;

 done:
    /* Convert back from base 4 to base 2. */
    a->exp >>= 1;
    if (!(a->frac_hi & DECOMPOSED_IMPLICIT_BIT)) {
        fracN(add)(a, a, a);
    } else {
        a->exp += 1;
    }
    return;

 d_nan:
    float_raise(float_flag_invalid | float_flag_invalid_sqrt, status);
    *a = partsN(default_nan)(status);
}

/*
 * Rounds the floating-point value `a' to an integer, and returns the
 * result as a floating-point value. The operation is performed
 * according to the IEC/IEEE Standard for Binary Floating-Point
 * Arithmetic.
 *
 * partsN(round_to_int_normal) is an internal helper function for
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
                fracN(add)(&tmp, a, a);
                /* Anything remaining means frac > 0.5. */
                one = !fracN(eqz)(&tmp);
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

        fracN(clear)(a);
        a->exp = 0;
        if (one) {
            a->frac_hi = DECOMPOSED_IMPLICIT_BIT;
        } else {
            a->cls = float_class_zero;
        }
        return true;
    }

    if (N > 64 && a->exp < N - 64) {
        /*
         * Rounding is not in the low word -- shift lsb to bit 2,
         * which leaves room for sticky and rounding bit.
         */
        shift_adj = (N - 1) - (a->exp + 2);
        fracN(shrjam)(a, shift_adj);
        frac_lsb = 1 << 2;
    } else {
        /*
         * Rounding is in the low word -- compute the lsb offset for rounding
         * and for clamping to the target precision, then map it to an offset
         * within frac_lo.
         */
        shift_adj = 0;
        frac_lsb = DECOMPOSED_IMPLICIT_BIT >> (MIN(a->exp, frac_size) & 63);
    }

    frac_lsbm1 = frac_lsb >> 1;
    rnd_mask = frac_lsb - 1;
    rnd_even_mask = rnd_mask | frac_lsb;

    if (!(a->frac_lo & rnd_mask)) {
        /* Fractional bits already clear, undo the shift above. */
        fracN(shl)(a, shift_adj);
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
        if (fracN(addi)(a, a, inc)) {
            fracN(shr)(a, 1);
            a->frac_hi |= DECOMPOSED_IMPLICIT_BIT;
            a->exp++;
        }
        a->frac_lo &= ~rnd_mask;
    } else {
        fracN(addi)(a, a, inc);
        a->frac_lo &= ~rnd_mask;
        /* Be careful shifting back, not to overflow */
        fracN(shl)(a, shift_adj - 1);
        if (a->frac_hi & DECOMPOSED_IMPLICIT_BIT) {
            a->exp++;
        } else {
            fracN(add)(a, a, a);
        }
    }
    return true;
}

FloatPartsN partsN(round_to_int)(const FloatPartsN *a,
                                 FloatRoundMode rmode,
                                 int scale, float_status *s,
                                 const FloatFmt *fmt)
{
    switch (a->cls) {
    case float_class_qnan:
    case float_class_snan:
        return partsN(return_nan)(a, s);
    case float_class_zero:
    case float_class_inf:
        return *a;
    case float_class_normal:
    case float_class_denormal:
        {
            FloatPartsN r = *a;
            if (partsN(round_to_int_normal)(&r, rmode, scale, fmt->frac_size)) {
                float_raise(float_flag_inexact, s);
            }
            return r;
        }
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
        flags |= float_flag_invalid_snan;
        /* fall through */
    case float_class_qnan:
        flags |= float_flag_invalid;
        r = max;
        break;

    case float_class_inf:
        flags = float_flag_invalid | float_flag_invalid_cvti;
        r = p->sign ? min : max;
        break;

    case float_class_zero:
        return 0;

    case float_class_normal:
    case float_class_denormal:
        /* TODO: N - 2 is frac_size for rounding; could use input fmt. */
        if (partsN(round_to_int_normal)(p, rmode, scale, N - 2)) {
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
                flags = float_flag_invalid | float_flag_invalid_cvti;
                r = min;
            }
        } else if (r > max) {
            flags = float_flag_invalid | float_flag_invalid_cvti;
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
        flags |= float_flag_invalid_snan;
        /* fall through */
    case float_class_qnan:
        flags |= float_flag_invalid;
        r = max;
        break;

    case float_class_inf:
        flags = float_flag_invalid | float_flag_invalid_cvti;
        r = p->sign ? 0 : max;
        break;

    case float_class_zero:
        return 0;

    case float_class_normal:
    case float_class_denormal:
        /* TODO: N - 2 is frac_size for rounding; could use input fmt. */
        if (partsN(round_to_int_normal)(p, rmode, scale, N - 2)) {
            flags = float_flag_inexact;
            if (p->cls == float_class_zero) {
                r = 0;
                break;
            }
        }

        if (p->sign) {
            flags = float_flag_invalid | float_flag_invalid_cvti;
            r = 0;
        } else if (p->exp > DECOMPOSED_BINARY_POINT) {
            flags = float_flag_invalid | float_flag_invalid_cvti;
            r = max;
        } else {
            r = p->frac_hi >> (DECOMPOSED_BINARY_POINT - p->exp);
            if (r > max) {
                flags = float_flag_invalid | float_flag_invalid_cvti;
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

/*
 * Float min/max.
 */
static FloatPartsN *partsN(minmax)(FloatPartsN *a, FloatPartsN *b,
                                   float_status *s, int flags)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);
    int a_exp, b_exp, cmp;

    if (unlikely(ab_mask & float_cmask_anynan)) {
        /*
         * For minNum/maxNum (IEEE 754-2008)
         * or minimumNumber/maximumNumber (IEEE 754-2019),
         * if one operand is a QNaN, and the other
         * operand is numerical, then return numerical argument.
         */
        if ((flags & (minmax_isnum | minmax_isnumber))
            && !(ab_mask & float_cmask_snan)
            && (ab_mask & ~float_cmask_qnan)) {
            record_denormals_used(ab_mask, s);
            return is_nan(a->cls) ? b : a;
        }

        /*
         * In IEEE 754-2019, minNum, maxNum, minNumMag and maxNumMag
         * are removed and replaced with minimum, minimumNumber, maximum
         * and maximumNumber.
         * minimumNumber/maximumNumber behavior for SNaN is changed to:
         *   If both operands are NaNs, a QNaN is returned.
         *   If either operand is a SNaN,
         *   an invalid operation exception is signaled,
         *   but unless both operands are NaNs,
         *   the SNaN is otherwise ignored and not converted to a QNaN.
         */
        if ((flags & minmax_isnumber)
            && (ab_mask & float_cmask_snan)
            && (ab_mask & ~float_cmask_anynan)) {
            float_raise(float_flag_invalid, s);
            return is_nan(a->cls) ? b : a;
        }

        *a = partsN(pick_nan)(a, b, s);
        return a;
    }

    record_denormals_used(ab_mask, s);

    a_exp = a->exp;
    b_exp = b->exp;

    if (unlikely(!cmask_is_only_normals(ab_mask))) {
        switch (a->cls) {
        case float_class_normal:
        case float_class_denormal:
            break;
        case float_class_inf:
            a_exp = INT16_MAX;
            break;
        case float_class_zero:
            a_exp = INT16_MIN;
            break;
        default:
            g_assert_not_reached();
        }
        switch (b->cls) {
        case float_class_normal:
        case float_class_denormal:
            break;
        case float_class_inf:
            b_exp = INT16_MAX;
            break;
        case float_class_zero:
            b_exp = INT16_MIN;
            break;
        default:
            g_assert_not_reached();
        }
    }

    /* Compare magnitudes. */
    cmp = a_exp - b_exp;
    if (cmp == 0) {
        cmp = fracN(cmp)(a, b);
    }

    /*
     * Take the sign into account.
     * For ismag, only do this if the magnitudes are equal.
     */
    if (!(flags & minmax_ismag) || cmp == 0) {
        if (a->sign != b->sign) {
            /* For differing signs, the negative operand is less. */
            cmp = a->sign ? -1 : 1;
        } else if (a->sign) {
            /* For two negative operands, invert the magnitude comparison. */
            cmp = -cmp;
        }
    }

    if (flags & minmax_ismin) {
        cmp = -cmp;
    }
    return cmp < 0 ? b : a;
}

/*
 * Floating point compare
 */
FloatRelation partsN(compare)(const FloatPartsN *a, const FloatPartsN *b,
                              float_status *s, bool is_quiet)
{
    int ab_mask = float_cmask(a->cls) | float_cmask(b->cls);

    if (likely(cmask_is_only_normals(ab_mask))) {
        FloatRelation cmp;

        record_denormals_used(ab_mask, s);

        if (a->sign != b->sign) {
            goto a_sign;
        }
        if (a->exp == b->exp) {
            cmp = fracN(cmp)(a, b);
        } else if (a->exp < b->exp) {
            cmp = float_relation_less;
        } else {
            cmp = float_relation_greater;
        }
        if (a->sign) {
            cmp = -cmp;
        }
        return cmp;
    }

    if (unlikely(ab_mask & float_cmask_anynan)) {
        if (ab_mask & float_cmask_snan) {
            float_raise(float_flag_invalid | float_flag_invalid_snan, s);
        } else if (!is_quiet) {
            float_raise(float_flag_invalid, s);
        }
        return float_relation_unordered;
    }

    record_denormals_used(ab_mask, s);

    if (ab_mask & float_cmask_zero) {
        if (ab_mask == float_cmask_zero) {
            return float_relation_equal;
        } else if (a->cls == float_class_zero) {
            goto b_sign;
        } else {
            goto a_sign;
        }
    }

    if (ab_mask == float_cmask_inf) {
        if (a->sign == b->sign) {
            return float_relation_equal;
        }
    } else if (b->cls == float_class_inf) {
        goto b_sign;
    } else {
        g_assert(a->cls == float_class_inf);
    }

 a_sign:
    return a->sign ? float_relation_less : float_relation_greater;
 b_sign:
    return b->sign ? float_relation_greater : float_relation_less;
}

/*
 * Multiply A by 2 raised to the power N.
 */
FloatPartsN partsN(scalbn)(const FloatPartsN *a, int n, float_status *s)
{
    switch (a->cls) {
    case float_class_snan:
    case float_class_qnan:
        return partsN(return_nan)(a, s);
    case float_class_zero:
    case float_class_inf:
        return *a;
    case float_class_denormal:
        float_raise(float_flag_input_denormal_used, s);
        /* fall through */
    case float_class_normal:
        {
            FloatPartsN r = *a;
            r.exp += MIN(MAX(n, -0x10000), 0x10000);
            return r;
        }
    default:
        g_assert_not_reached();
    }
}
