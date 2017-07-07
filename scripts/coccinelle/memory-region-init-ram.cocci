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
