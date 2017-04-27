// Use QDict macros where they make sense
@@
expression Obj, Key, E;
@@
- qdict_put_obj(Obj, Key, QOBJECT(E));
+ qdict_put(Obj, Key, E);

// Use QList macros where they make sense
@@
expression Obj, E;
@@
- qlist_append_obj(Obj, QOBJECT(E));
+ qlist_append(Obj, E);
