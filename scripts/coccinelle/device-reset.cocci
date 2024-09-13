// Convert opencoded DeviceClass::reset assignments to calls to
// device_class_set_legacy_reset()
//
// Copyright Linaro Ltd 2024
// This work is licensed under the terms of the GNU GPLv2 or later.
//
// spatch --macro-file scripts/cocci-macro-file.h \
//        --sp-file scripts/coccinelle/device-reset.cocci \
//        --keep-comments --smpl-spacing --in-place --include-headers --dir hw
//
// For simplicity we assume some things about the code we're modifying
// that happen to be true for all our targets:
//  * all cpu_class_set_parent_reset() callsites have a 'DeviceClass *dc' local
//  * the parent reset field in the target CPU class is 'parent_reset'
//  * no reset function already has a 'dev' local

@@
identifier dc, resetfn;
@@
  DeviceClass *dc;
  ...
- dc->reset = resetfn;
+ device_class_set_legacy_reset(dc, resetfn);
@@
identifier dc, resetfn;
@@
  DeviceClass *dc;
  ...
- dc->reset = &resetfn;
+ device_class_set_legacy_reset(dc, resetfn);
