// Convert device code using three-phase reset to add a ResetType
// argument to implementations of ResettableHoldPhase and
// ResettableEnterPhase methods.
//
// Copyright Linaro Ltd 2024
// SPDX-License-Identifier: GPL-2.0-or-later
//
// for dir in include hw target; do \
// spatch --macro-file scripts/cocci-macro-file.h \
//        --sp-file scripts/coccinelle/reset-type.cocci \
//        --keep-comments --smpl-spacing --in-place --include-headers \
//        --dir $dir; done
//
// This coccinelle script aims to produce a complete change that needs
// no human interaction, so as well as the generic "update device
// implementations of the hold and exit phase methods" it includes
// the special-case transformations needed for the core code and for
// one device model that does something a bit nonstandard. Those
// special cases are at the end of the file.

// Look for where we use a function as a ResettableHoldPhase method,
// either by directly assigning it to phases.hold or by calling
// resettable_class_set_parent_phases, and remember the function name.
@ holdfn_assigned @
identifier enterfn, holdfn, exitfn;
identifier rc;
expression e;
@@
ResettableClass *rc;
...
(
 rc->phases.hold = holdfn;
|
 resettable_class_set_parent_phases(rc, enterfn, holdfn, exitfn, e);
)

// Look for the definition of the function we found in holdfn_assigned,
// and add the new argument. If the function calls a hold function
// itself (probably chaining to the parent class reset) then add the
// new argument there too.
@ holdfn_defined @
identifier holdfn_assigned.holdfn;
typedef Object;
identifier obj;
expression parent;
@@
-holdfn(Object *obj)
+holdfn(Object *obj, ResetType type)
{
    <...
-    parent.hold(obj)
+    parent.hold(obj, type)
    ...>
}

// Similarly for ResettableExitPhase.
@ exitfn_assigned @
identifier enterfn, holdfn, exitfn;
identifier rc;
expression e;
@@
ResettableClass *rc;
...
(
 rc->phases.exit = exitfn;
|
 resettable_class_set_parent_phases(rc, enterfn, holdfn, exitfn, e);
)
@ exitfn_defined @
identifier exitfn_assigned.exitfn;
typedef Object;
identifier obj;
expression parent;
@@
-exitfn(Object *obj)
+exitfn(Object *obj, ResetType type)
{
    <...
-    parent.exit(obj)
+    parent.exit(obj, type)
    ...>
}

// SPECIAL CASES ONLY BELOW HERE
// We use a python scripted constraint on the position of the match
// to ensure that they only match in a particular function. See
// https://public-inbox.org/git/alpine.DEB.2.21.1808240652370.2344@hadrien/
// which recommends this as the way to do "match only in this function".

// Special case: isl_pmbus_vr.c has some reset methods calling others directly
@ isl_pmbus_vr @
identifier obj;
@@
- isl_pmbus_vr_exit_reset(obj);
+ isl_pmbus_vr_exit_reset(obj, type);

// Special case: device_phases_reset() needs to pass RESET_TYPE_COLD
@ device_phases_reset_hold @
expression obj;
identifier rc;
identifier phase;
position p : script:python() { p[0].current_element == "device_phases_reset" };
@@
- rc->phases.phase(obj)@p
+ rc->phases.phase(obj, RESET_TYPE_COLD)

// Special case: in resettable_phase_hold() and resettable_phase_exit()
// we need to pass through the ResetType argument to the method being called
@ resettable_phase_hold @
expression obj;
identifier rc;
position p : script:python() { p[0].current_element == "resettable_phase_hold" };
@@
- rc->phases.hold(obj)@p
+ rc->phases.hold(obj, type)
@ resettable_phase_exit @
expression obj;
identifier rc;
position p : script:python() { p[0].current_element == "resettable_phase_exit" };
@@
- rc->phases.exit(obj)@p
+ rc->phases.exit(obj, type)
// Special case: the typedefs for the methods need to declare the new argument
@ phase_typedef_hold @
identifier obj;
@@
- typedef void (*ResettableHoldPhase)(Object *obj);
+ typedef void (*ResettableHoldPhase)(Object *obj, ResetType type);
@ phase_typedef_exit @
identifier obj;
@@
- typedef void (*ResettableExitPhase)(Object *obj);
+ typedef void (*ResettableExitPhase)(Object *obj, ResetType type);
