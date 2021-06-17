/*
  Usage:

    spatch \
        --macro-file scripts/cocci-macro-file.h \
        --sp-file scripts/coccinelle/memory-region-housekeeping.cocci \
        --keep-comments \
        --in-place \
        --dir .

*/


// Replace memory_region_init_ram(readonly) by memory_region_init_rom()
@@
expression E1, E2, E3, E4, E5;
symbol true;
@@
(
- memory_region_init_ram(E1, E2, E3, E4, E5);
+ memory_region_init_rom(E1, E2, E3, E4, E5);
  ... WHEN != E1
- memory_region_set_readonly(E1, true);
|
- memory_region_init_ram_nomigrate(E1, E2, E3, E4, E5);
+ memory_region_init_rom_nomigrate(E1, E2, E3, E4, E5);
  ... WHEN != E1
- memory_region_set_readonly(E1, true);
)


@possible_memory_region_init_rom@
expression E1, E2, E3, E4, E5;
position p;
@@
(
  memory_region_init_ram@p(E1, E2, E3, E4, E5);
  ...
  memory_region_set_readonly(E1, true);
|
  memory_region_init_ram_nomigrate@p(E1, E2, E3, E4, E5);
  ...
  memory_region_set_readonly(E1, true);
)
@script:python@
p << possible_memory_region_init_rom.p;
@@
cocci.print_main("potential use of memory_region_init_rom*() in ", p)


// Do not call memory_region_set_readonly() on ROM alias
@@
expression ROM, E1, E2, E3, E4;
expression ALIAS, E5, E6, E7, E8;
@@
(
  memory_region_init_rom(ROM, E1, E2, E3, E4);
|
  memory_region_init_rom_nomigrate(ROM, E1, E2, E3, E4);
)
  ...
   memory_region_init_alias(ALIAS, E5, E6, ROM, E7, E8);
-  memory_region_set_readonly(ALIAS, true);


// Replace by-hand memory_region_init_ram_nomigrate/vmstate_register_ram
// code sequences with use of the new memory_region_init_ram function.
// Similarly for the _rom and _rom_device functions.
// We don't try to replace sequences with a non-NULL owner, because
// there are none in the tree that can be automatically converted
// (and only a handful that can be manually converted).
@@
expression MR;
expression NAME;
expression SIZE;
expression ERRP;
@@
-memory_region_init_ram_nomigrate(MR, NULL, NAME, SIZE, ERRP);
+memory_region_init_ram(MR, NULL, NAME, SIZE, ERRP);
 ...
-vmstate_register_ram_global(MR);
@@
expression MR;
expression NAME;
expression SIZE;
expression ERRP;
@@
-memory_region_init_rom_nomigrate(MR, NULL, NAME, SIZE, ERRP);
+memory_region_init_rom(MR, NULL, NAME, SIZE, ERRP);
 ...
-vmstate_register_ram_global(MR);
@@
expression MR;
expression OPS;
expression OPAQUE;
expression NAME;
expression SIZE;
expression ERRP;
@@
-memory_region_init_rom_device_nomigrate(MR, NULL, OPS, OPAQUE, NAME, SIZE, ERRP);
+memory_region_init_rom_device(MR, NULL, OPS, OPAQUE, NAME, SIZE, ERRP);
 ...
-vmstate_register_ram_global(MR);


// Device is owner
@@
typedef DeviceState;
identifier device_fn, dev, obj;
expression E1, E2, E3, E4, E5;
@@
static void device_fn(DeviceState *dev, ...)
{
  ...
  Object *obj = OBJECT(dev);
  <+...
(
- memory_region_init(E1, NULL, E2, E3);
+ memory_region_init(E1, obj, E2, E3);
|
- memory_region_init_io(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_io(E1, obj, E2, E3, E4, E5);
|
- memory_region_init_alias(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_alias(E1, obj, E2, E3, E4, E5);
|
- memory_region_init_rom(E1, NULL, E2, E3, E4);
+ memory_region_init_rom(E1, obj, E2, E3, E4);
|
- memory_region_init_ram_flags_nomigrate(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_ram_flags_nomigrate(E1, obj, E2, E3, E4, E5);
)
  ...+>
}
@@
identifier device_fn, dev;
expression E1, E2, E3, E4, E5;
@@
static void device_fn(DeviceState *dev, ...)
{
  <+...
(
- memory_region_init(E1, NULL, E2, E3);
+ memory_region_init(E1, OBJECT(dev), E2, E3);
|
- memory_region_init_io(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_io(E1, OBJECT(dev), E2, E3, E4, E5);
|
- memory_region_init_alias(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_alias(E1, OBJECT(dev), E2, E3, E4, E5);
|
- memory_region_init_rom(E1, NULL, E2, E3, E4);
+ memory_region_init_rom(E1, OBJECT(dev), E2, E3, E4);
|
- memory_region_init_ram_flags_nomigrate(E1, NULL, E2, E3, E4, E5);
+ memory_region_init_ram_flags_nomigrate(E1, OBJECT(dev), E2, E3, E4, E5);
)
  ...+>
}
