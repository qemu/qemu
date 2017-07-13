// Use QDict macros where they make sense
@@
expression Obj, Key, E;
@@
(
- qobject_incref(QOBJECT(E));
+ QINCREF(E);
|
- qobject_decref(QOBJECT(E));
+ QDECREF(E);
|
- qdict_put_obj(Obj, Key, QOBJECT(E));
+ qdict_put(Obj, Key, E);
|
- qdict_put(Obj, Key, qnum_from_int(E));
+ qdict_put_int(Obj, Key, E);
|
- qdict_put(Obj, Key, qbool_from_bool(E));
+ qdict_put_bool(Obj, Key, E);
|
- qdict_put(Obj, Key, qstring_from_str(E));
+ qdict_put_str(Obj, Key, E);
)

// Use QList macros where they make sense
@@
expression Obj, E;
@@
(
- qlist_append_obj(Obj, QOBJECT(E));
+ qlist_append(Obj, E);
|
- qlist_append(Obj, qnum_from_int(E));
+ qlist_append_int(Obj, E);
|
- qlist_append(Obj, qbool_from_bool(E));
+ qlist_append_bool(Obj, E);
|
- qlist_append(Obj, qstring_from_str(E));
+ qlist_append_str(Obj, E);
)
