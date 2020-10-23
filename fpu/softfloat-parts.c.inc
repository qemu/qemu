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
