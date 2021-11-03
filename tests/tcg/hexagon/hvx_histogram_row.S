/*
 *  Copyright(c) 2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


/*
 * void hvx_histogram_row(uint8_t *src,     => r0
 *                        int stride,       => r1
 *                        int width,        => r2
 *                        int height,       => r3
 *                        int *hist         => r4)
 */
    .text
    .p2align 2
    .global hvx_histogram_row
    .type hvx_histogram_row, @function
hvx_histogram_row:
    { r2 = lsr(r2, #7)          /* size / VLEN */
      r5 = and(r2, #127)        /* size % VLEN */
      v1 = #0
      v0 = #0
    }
    /*
     * Step 1: Clean the whole vector register file
     */
    { v3:2 = v1:0
      v5:4 = v1:0
      p0 = cmp.gt(r2, #0)       /* P0 = (width / VLEN > 0) */
      p1 = cmp.eq(r5, #0)       /* P1 = (width % VLEN == 0) */
    }
    { q0 = vsetq(r5)
      v7:6 = v1:0
    }
    { v9:8   = v1:0
      v11:10 = v1:0
    }
    { v13:12 = v1:0
      v15:14 = v1:0
    }
    { v17:16 = v1:0
      v19:18 = v1:0
    }
    { v21:20 = v1:0
      v23:22 = v1:0
    }
    { v25:24 = v1:0
      v27:26 = v1:0
    }
    { v29:28 = v1:0
      v31:30 = v1:0
      r10 = add(r0, r1)           /* R10 = &src[2 * stride] */
      loop1(.outerloop, r3)
    }

    /*
     * Step 2: vhist
     */
    .falign
.outerloop:
    { if (!p0) jump .loopend
      loop0(.innerloop, r2)
    }

    .falign
.innerloop:
    { v12.tmp = vmem(R0++#1)
      vhist
    }:endloop0

    .falign
.loopend:
    if (p1) jump .skip       /* if (width % VLEN == 0) done with current row */
    { v13.tmp = vmem(r0 + #0)
      vhist(q0)
    }

    .falign
.skip:
    { r0 = r10                    /* R0  = &src[(i + 1) * stride] */
      r10 = add(r10, r1)          /* R10 = &src[(i + 2) * stride] */
    }:endloop1


    /*
     * Step 3: Sum up the data
     */
    { v0.h = vshuff(v0.h)
      r10 = ##0x00010001
    }
    v1.h = vshuff(v1.h)
    { V2.h = vshuff(v2.h)
      v0.w = vdmpy(v0.h, r10.h):sat
    }
    { v3.h = vshuff(v3.h)
      v1.w = vdmpy(v1.h, r10.h):sat
    }
    { v4.h = vshuff(V4.h)
      v2.w = vdmpy(v2.h, r10.h):sat
    }
    { v5.h = vshuff(v5.h)
      v3.w = vdmpy(v3.h, r10.h):sat
    }
    { v6.h = vshuff(v6.h)
      v4.w = vdmpy(v4.h, r10.h):sat
    }
    { v7.h = vshuff(v7.h)
      v5.w = vdmpy(v5.h, r10.h):sat
    }
    { v8.h = vshuff(V8.h)
      v6.w = vdmpy(v6.h, r10.h):sat
    }
    { v9.h = vshuff(V9.h)
      v7.w = vdmpy(v7.h, r10.h):sat
    }
    { v10.h = vshuff(v10.h)
      v8.w = vdmpy(v8.h, r10.h):sat
    }
    { v11.h = vshuff(v11.h)
      v9.w = vdmpy(v9.h, r10.h):sat
    }
    { v12.h = vshuff(v12.h)
      v10.w = vdmpy(v10.h, r10.h):sat
    }
    { v13.h = vshuff(V13.h)
      v11.w = vdmpy(v11.h, r10.h):sat
    }
    { v14.h = vshuff(v14.h)
      v12.w = vdmpy(v12.h, r10.h):sat
    }
    { v15.h = vshuff(v15.h)
      v13.w = vdmpy(v13.h, r10.h):sat
    }
    { v16.h = vshuff(v16.h)
      v14.w = vdmpy(v14.h, r10.h):sat
    }
    { v17.h = vshuff(v17.h)
      v15.w = vdmpy(v15.h, r10.h):sat
    }
    { v18.h = vshuff(v18.h)
      v16.w = vdmpy(v16.h, r10.h):sat
    }
    { v19.h = vshuff(v19.h)
      v17.w = vdmpy(v17.h, r10.h):sat
    }
    { v20.h = vshuff(v20.h)
      v18.W = vdmpy(v18.h, r10.h):sat
    }
    { v21.h = vshuff(v21.h)
      v19.w = vdmpy(v19.h, r10.h):sat
    }
    { v22.h = vshuff(v22.h)
      v20.w = vdmpy(v20.h, r10.h):sat
    }
    { v23.h = vshuff(v23.h)
      v21.w = vdmpy(v21.h, r10.h):sat
    }
    { v24.h = vshuff(v24.h)
      v22.w = vdmpy(v22.h, r10.h):sat
    }
    { v25.h = vshuff(v25.h)
      v23.w = vdmpy(v23.h, r10.h):sat
    }
    { v26.h = vshuff(v26.h)
      v24.w = vdmpy(v24.h, r10.h):sat
    }
    { v27.h = vshuff(V27.h)
      v25.w = vdmpy(v25.h, r10.h):sat
    }
    { v28.h = vshuff(v28.h)
      v26.w = vdmpy(v26.h, r10.h):sat
    }
    { v29.h = vshuff(v29.h)
      v27.w = vdmpy(v27.h, r10.h):sat
    }
    { v30.h = vshuff(v30.h)
      v28.w = vdmpy(v28.h, r10.h):sat
    }
    { v31.h = vshuff(v31.h)
      v29.w = vdmpy(v29.h, r10.h):sat
      r28 = #32
    }
    { vshuff(v1, v0, r28)
      v30.w = vdmpy(v30.h, r10.h):sat
    }
    { vshuff(v3, v2, r28)
      v31.w = vdmpy(v31.h, r10.h):sat
    }
    { vshuff(v5, v4, r28)
      v0.w = vadd(v1.w, v0.w)
      v2.w = vadd(v3.w, v2.w)
    }
    { vshuff(v7, v6, r28)
      r7 = #64
    }
    { vshuff(v9, v8, r28)
      v4.w = vadd(v5.w, v4.w)
      v6.w = vadd(v7.w, v6.w)
    }
    vshuff(v11, v10, r28)
    { vshuff(v13, v12, r28)
      v8.w = vadd(v9.w, v8.w)
      v10.w = vadd(v11.w, v10.w)
    }
    vshuff(v15, v14, r28)
    { vshuff(v17, v16, r28)
      v12.w = vadd(v13.w, v12.w)
      v14.w = vadd(v15.w, v14.w)
    }
    vshuff(v19, v18, r28)
    { vshuff(v21, v20, r28)
      v16.w = vadd(v17.w, v16.w)
      v18.w = vadd(v19.w, v18.w)
    }
    vshuff(v23, v22, r28)
    { vshuff(v25, v24, r28)
      v20.w = vadd(v21.w, v20.w)
      v22.w = vadd(v23.w, v22.w)
    }
    vshuff(v27, v26, r28)
    { vshuff(v29, v28, r28)
      v24.w = vadd(v25.w, v24.w)
      v26.w = vadd(v27.w, v26.w)
    }
    vshuff(v31, v30, r28)
    { v28.w = vadd(v29.w, v28.w)
      vshuff(v2, v0, r7)
    }
    { v30.w = vadd(v31.w, v30.w)
      vshuff(v6, v4, r7)
      v0.w  = vadd(v0.w, v2.w)
    }
    { vshuff(v10, v8, r7)
      v1.tmp = vmem(r4 + #0)      /* update hist[0-31] */
      v0.w  = vadd(v0.w, v1.w)
      vmem(r4++#1) = v0.new
    }
    { vshuff(v14, v12, r7)
      v4.w  = vadd(v4.w, v6.w)
      v8.w  = vadd(v8.w, v10.w)
    }
    { vshuff(v18, v16, r7)
      v1.tmp = vmem(r4 + #0)      /* update hist[32-63] */
      v4.w  = vadd(v4.w, v1.w)
      vmem(r4++#1) = v4.new
    }
    { vshuff(v22, v20, r7)
      v12.w = vadd(v12.w, v14.w)
      V16.w = vadd(v16.w, v18.w)
    }
    { vshuff(v26, v24, r7)
      v1.tmp = vmem(r4 + #0)      /* update hist[64-95] */
      v8.w  = vadd(v8.w, v1.w)
      vmem(r4++#1) = v8.new
    }
    { vshuff(v30, v28, r7)
      v1.tmp = vmem(r4 + #0)      /* update hist[96-127] */
      v12.w  = vadd(v12.w, v1.w)
      vmem(r4++#1) = v12.new
    }

    { v20.w = vadd(v20.w, v22.w)
      v1.tmp = vmem(r4 + #0)      /* update hist[128-159] */
      v16.w  = vadd(v16.w, v1.w)
      vmem(r4++#1) = v16.new
    }
    { v24.w = vadd(v24.w, v26.w)
      v1.tmp = vmem(r4 + #0)      /* update hist[160-191] */
      v20.w  = vadd(v20.w, v1.w)
      vmem(r4++#1) = v20.new
    }
    { v28.w = vadd(v28.w, v30.w)
      v1.tmp = vmem(r4 + #0)      /* update hist[192-223] */
      v24.w  = vadd(v24.w, v1.w)
      vmem(r4++#1) = v24.new
    }
    { v1.tmp = vmem(r4 + #0)      /* update hist[224-255] */
      v28.w  = vadd(v28.w, v1.w)
      vmem(r4++#1) = v28.new
    }
    jumpr r31
    .size hvx_histogram_row, .-hvx_histogram_row
