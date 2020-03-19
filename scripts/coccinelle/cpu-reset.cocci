// Convert targets using the old CPUState reset to DeviceState reset
//
// Copyright Linaro Ltd 2020
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch --macro-file scripts/cocci-macro-file.h \
//        --sp-file scripts/coccinelle/cpu-reset.cocci \
//        --keep-comments --smpl-spacing --in-place --include-headers --dir target
//
// For simplicity we assume some things about the code we're modifying
// that happen to be true for all our targets:
//  * all cpu_class_set_parent_reset() callsites have a 'DeviceClass *dc' local
//  * the parent reset field in the target CPU class is 'parent_reset'
//  * no reset function already has a 'dev' local

@@
identifier cpu, x;
typedef CPUState;
@@
struct x {
...
- void (*parent_reset)(CPUState *cpu);
+ DeviceReset parent_reset;
...
};
@ rule1 @
identifier resetfn;
expression resetfield;
identifier cc;
@@
- cpu_class_set_parent_reset(cc, resetfn, resetfield)
+ device_class_set_parent_reset(dc, resetfn, resetfield)
@@
identifier rule1.resetfn;
identifier cpu, cc;
typedef CPUState, DeviceState;
@@
-resetfn(CPUState *cpu)
-{
+resetfn(DeviceState *dev)
+{
+    CPUState *cpu = CPU(dev);
<...
-    cc->parent_reset(cpu);
+    cc->parent_reset(dev);
...>
}
