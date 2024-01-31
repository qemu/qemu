/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_RECT_H
#define QEMU_RECT_H


typedef struct QemuRect {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} QemuRect;

static inline void qemu_rect_init(QemuRect *rect,
                                  int16_t x, int16_t y,
                                  uint16_t width, uint16_t height)
{
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
}

static inline void qemu_rect_translate(QemuRect *rect,
                                       int16_t dx, int16_t dy)
{
    rect->x += dx;
    rect->y += dy;
}

static inline bool qemu_rect_intersect(const QemuRect *a, const QemuRect *b,
                                       QemuRect *res)
{
    int16_t x1, x2, y1, y2;

    x1 = MAX(a->x, b->x);
    y1 = MAX(a->y, b->y);
    x2 = MIN(a->x + a->width, b->x + b->width);
    y2 = MIN(a->y + a->height, b->y + b->height);

    if (x1 >= x2 || y1 >= y2) {
        if (res) {
            qemu_rect_init(res, 0, 0, 0, 0);
        }

        return false;
    }

    if (res) {
        qemu_rect_init(res, x1, y1, x2 - x1, y2 - y1);
    }

    return true;
}

#endif
