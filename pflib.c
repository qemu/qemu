/*
 * PixelFormat conversion library.
 *
 * Author: Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "qemu-common.h"
#include "console.h"
#include "pflib.h"

typedef struct QemuPixel QemuPixel;

typedef void (*pf_convert)(QemuPfConv *conv,
                           void *dst, void *src, uint32_t cnt);
typedef void (*pf_convert_from)(PixelFormat *pf,
                                QemuPixel *dst, void *src, uint32_t cnt);
typedef void (*pf_convert_to)(PixelFormat *pf,
                              void *dst, QemuPixel *src, uint32_t cnt);

struct QemuPfConv {
    pf_convert        convert;
    PixelFormat       src;
    PixelFormat       dst;

    /* for copy_generic() */
    pf_convert_from   conv_from;
    pf_convert_to     conv_to;
    QemuPixel         *conv_buf;
    uint32_t          conv_cnt;
};

struct QemuPixel {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

/* ----------------------------------------------------------------------- */
/* PixelFormat -> QemuPixel conversions                                    */

static void conv_16_to_pixel(PixelFormat *pf,
                             QemuPixel *dst, void *src, uint32_t cnt)
{
    uint16_t *src16 = src;

    while (cnt > 0) {
        dst->red   = ((*src16 & pf->rmask) >> pf->rshift) << (8 - pf->rbits);
        dst->green = ((*src16 & pf->gmask) >> pf->gshift) << (8 - pf->gbits);
        dst->blue  = ((*src16 & pf->bmask) >> pf->bshift) << (8 - pf->bbits);
        dst->alpha = ((*src16 & pf->amask) >> pf->ashift) << (8 - pf->abits);
        dst++, src16++, cnt--;
    }
}

/* assumes pf->{r,g,b,a}bits == 8 */
static void conv_32_to_pixel_fast(PixelFormat *pf,
                                  QemuPixel *dst, void *src, uint32_t cnt)
{
    uint32_t *src32 = src;

    while (cnt > 0) {
        dst->red   = (*src32 & pf->rmask) >> pf->rshift;
        dst->green = (*src32 & pf->gmask) >> pf->gshift;
        dst->blue  = (*src32 & pf->bmask) >> pf->bshift;
        dst->alpha = (*src32 & pf->amask) >> pf->ashift;
        dst++, src32++, cnt--;
    }
}

static void conv_32_to_pixel_generic(PixelFormat *pf,
                                     QemuPixel *dst, void *src, uint32_t cnt)
{
    uint32_t *src32 = src;

    while (cnt > 0) {
        if (pf->rbits < 8) {
            dst->red   = ((*src32 & pf->rmask) >> pf->rshift) << (8 - pf->rbits);
        } else {
            dst->red   = ((*src32 & pf->rmask) >> pf->rshift) >> (pf->rbits - 8);
        }
        if (pf->gbits < 8) {
            dst->green = ((*src32 & pf->gmask) >> pf->gshift) << (8 - pf->gbits);
        } else {
            dst->green = ((*src32 & pf->gmask) >> pf->gshift) >> (pf->gbits - 8);
        }
        if (pf->bbits < 8) {
            dst->blue  = ((*src32 & pf->bmask) >> pf->bshift) << (8 - pf->bbits);
        } else {
            dst->blue  = ((*src32 & pf->bmask) >> pf->bshift) >> (pf->bbits - 8);
        }
        if (pf->abits < 8) {
            dst->alpha = ((*src32 & pf->amask) >> pf->ashift) << (8 - pf->abits);
        } else {
            dst->alpha = ((*src32 & pf->amask) >> pf->ashift) >> (pf->abits - 8);
        }
        dst++, src32++, cnt--;
    }
}

/* ----------------------------------------------------------------------- */
/* QemuPixel -> PixelFormat conversions                                    */

static void conv_pixel_to_16(PixelFormat *pf,
                             void *dst, QemuPixel *src, uint32_t cnt)
{
    uint16_t *dst16 = dst;

    while (cnt > 0) {
        *dst16  = ((uint16_t)src->red   >> (8 - pf->rbits)) << pf->rshift;
        *dst16 |= ((uint16_t)src->green >> (8 - pf->gbits)) << pf->gshift;
        *dst16 |= ((uint16_t)src->blue  >> (8 - pf->bbits)) << pf->bshift;
        *dst16 |= ((uint16_t)src->alpha >> (8 - pf->abits)) << pf->ashift;
        dst16++, src++, cnt--;
    }
}

static void conv_pixel_to_32(PixelFormat *pf,
                             void *dst, QemuPixel *src, uint32_t cnt)
{
    uint32_t *dst32 = dst;

    while (cnt > 0) {
        *dst32  = ((uint32_t)src->red   >> (8 - pf->rbits)) << pf->rshift;
        *dst32 |= ((uint32_t)src->green >> (8 - pf->gbits)) << pf->gshift;
        *dst32 |= ((uint32_t)src->blue  >> (8 - pf->bbits)) << pf->bshift;
        *dst32 |= ((uint32_t)src->alpha >> (8 - pf->abits)) << pf->ashift;
        dst32++, src++, cnt--;
    }
}

/* ----------------------------------------------------------------------- */
/* PixelFormat -> PixelFormat conversions                                  */

static void convert_copy(QemuPfConv *conv, void *dst, void *src, uint32_t cnt)
{
    uint32_t bytes = cnt * conv->src.bytes_per_pixel;
    memcpy(dst, src, bytes);
}

static void convert_generic(QemuPfConv *conv, void *dst, void *src, uint32_t cnt)
{
    if (conv->conv_cnt < cnt) {
        conv->conv_cnt = cnt;
        conv->conv_buf = g_realloc(conv->conv_buf, sizeof(QemuPixel) * conv->conv_cnt);
    }
    conv->conv_from(&conv->src, conv->conv_buf, src, cnt);
    conv->conv_to(&conv->dst, dst, conv->conv_buf, cnt);
}

/* ----------------------------------------------------------------------- */
/* public interface                                                        */

QemuPfConv *qemu_pf_conv_get(PixelFormat *dst, PixelFormat *src)
{
    QemuPfConv *conv = g_malloc0(sizeof(QemuPfConv));

    conv->src = *src;
    conv->dst = *dst;

    if (memcmp(&conv->src, &conv->dst, sizeof(PixelFormat)) == 0) {
        /* formats identical, can simply copy */
        conv->convert = convert_copy;
    } else {
        /* generic two-step conversion: src -> QemuPixel -> dst  */
        switch (conv->src.bytes_per_pixel) {
        case 2:
            conv->conv_from = conv_16_to_pixel;
            break;
        case 4:
            if (conv->src.rbits == 8 && conv->src.gbits == 8 && conv->src.bbits == 8) {
                conv->conv_from = conv_32_to_pixel_fast;
            } else {
                conv->conv_from = conv_32_to_pixel_generic;
            }
            break;
        default:
            goto err;
        }
        switch (conv->dst.bytes_per_pixel) {
        case 2:
            conv->conv_to = conv_pixel_to_16;
            break;
        case 4:
            conv->conv_to = conv_pixel_to_32;
            break;
        default:
            goto err;
        }
        conv->convert = convert_generic;
    }
    return conv;

err:
    g_free(conv);
    return NULL;
}

void qemu_pf_conv_run(QemuPfConv *conv, void *dst, void *src, uint32_t cnt)
{
    conv->convert(conv, dst, src, cnt);
}

void qemu_pf_conv_put(QemuPfConv *conv)
{
    if (conv) {
        g_free(conv->conv_buf);
        g_free(conv);
    }
}
