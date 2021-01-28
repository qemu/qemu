// Highlight object declarations that don't look like object class but
// accidentally inherit from it.

@match@
identifier obj_t, fld;
type parent_t =~ ".*Class$";
@@
struct obj_t {
    parent_t fld;
    ...
};

@script:python filter depends on match@
obj_t << match.obj_t;
@@
is_class_obj = obj_t.endswith('Class')
cocci.include_match(not is_class_obj)

@replacement depends on filter@
identifier match.obj_t, match.fld;
type match.parent_t;
@@
struct obj_t {
*   parent_t fld;
    ...
};
