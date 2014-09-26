/* QEMU accelerator interfaces
 *
 * Copyright (c) 2014 Red Hat Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef HW_ACCEL_H
#define HW_ACCEL_H

#include "qemu/typedefs.h"
#include "qom/object.h"

typedef struct AccelState {
    /*< private >*/
    Object parent_obj;
} AccelState;

typedef struct AccelClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    const char *opt_name;
    const char *name;
    int (*available)(void);
    int (*init_machine)(MachineState *ms);
    bool *allowed;
} AccelClass;

#define TYPE_ACCEL "accel"

#define ACCEL_CLASS_SUFFIX  "-" TYPE_ACCEL
#define ACCEL_CLASS_NAME(a) (a ACCEL_CLASS_SUFFIX)

#define ACCEL_CLASS(klass) \
    OBJECT_CLASS_CHECK(AccelClass, (klass), TYPE_ACCEL)
#define ACCEL(obj) \
    OBJECT_CHECK(AccelState, (obj), TYPE_ACCEL)
#define ACCEL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AccelClass, (obj), TYPE_ACCEL)

extern int tcg_tb_size;

int configure_accelerator(MachineState *ms);

#endif
