/*
 * QEMU Object Model
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_OBJECT_H
#define QEMU_OBJECT_H

#include "qapi/qapi-builtin-types.h"
#include "qemu/module.h"
#include "qom/object.h"

struct TypeImpl;
typedef struct TypeImpl *Type;

typedef struct TypeInfo TypeInfo;

typedef struct InterfaceClass InterfaceClass;
typedef struct InterfaceInfo InterfaceInfo;

#define TYPE_OBJECT "object"

typedef struct ObjectProperty ObjectProperty;

/**
 * typedef ObjectPropertyAccessor:
 * @obj: the object that owns the property
 * @v: the visitor that contains the property data
 * @name: the name of the property
 * @opaque: the object property opaque
 * @errp: a pointer to an Error that is filled if getting/setting fails.
 *
 * Called when trying to get/set a property.
 */
typedef void (ObjectPropertyAccessor)(Object *obj,
                                      Visitor *v,
                                      const char *name,
                                      void *opaque,
                                      Error **errp);

/**
 * typedef ObjectPropertyResolve:
 * @obj: the object that owns the property
 * @opaque: the opaque registered with the property
 * @part: the name of the property
 *
 * Resolves the #Object corresponding to property @part.
 *
 * The returned object can also be used as a starting point
 * to resolve a relative path starting with "@part".
 *
 * Returns: If @path is the path that led to @obj, the function
 * returns the #Object corresponding to "@path/@part".
 * If "@path/@part" is not a valid object path, it returns #NULL.
 */
typedef Object *(ObjectPropertyResolve)(Object *obj,
                                        void *opaque,
                                        const char *part);

/**
 * typedef ObjectPropertyRelease:
 * @obj: the object that owns the property
 * @name: the name of the property
 * @opaque: the opaque registered with the property
 *
 * Called when a property is removed from a object.
 */
typedef void (ObjectPropertyRelease)(Object *obj,
                                     const char *name,
                                     void *opaque);

/**
 * typedef ObjectPropertyInit:
 * @obj: the object that owns the property
 * @prop: the property to set
 *
 * Called when a property is initialized.
 */
typedef void (ObjectPropertyInit)(Object *obj, ObjectProperty *prop);

struct ObjectProperty
{
    char *name;
    char *type;
    char *description;
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyResolve *resolve;
    ObjectPropertyRelease *release;
    ObjectPropertyInit *init;
    void *opaque;
    QObject *defval;
};

/**
 * typedef ObjectUnparent:
 * @obj: the object that is being removed from the composition tree
 *
 * Called when an object is being removed from the QOM composition tree.
 * The function should remove any backlinks from children objects to @obj.
 */
typedef void (ObjectUnparent)(Object *obj);

/**
 * typedef ObjectFree:
 * @obj: the object being freed
 *
 * Called when an object's last reference is removed.
 */
typedef void (ObjectFree)(void *obj);

#define OBJECT_CLASS_CAST_CACHE 4

/**
 * struct ObjectClass:
 *
 * The base for all classes.  The only thing that #ObjectClass contains is an
 * integer type handle.
 */
struct ObjectClass
{
    /* private: */
    Type type;
    GSList *interfaces;

    const char *object_cast_cache[OBJECT_CLASS_CAST_CACHE];
    const char *class_cast_cache[OBJECT_CLASS_CAST_CACHE];

    ObjectUnparent *unparent;

    GHashTable *properties;
};

/**
 * struct Object:
 *
 * The base for all objects.  The first member of this object is a pointer to
 * a #ObjectClass.  Since C guarantees that the first member of a structure
 * always begins at byte 0 of that structure, as long as any sub-object places
 * its parent as the first member, we can cast directly to a #Object.
 *
 * As a result, #Object contains a reference to the objects type as its
 * first member.  This allows identification of the real type of the object at
 * run time.
 */
struct Object
{
    /* private: */
    ObjectClass *class;
    ObjectFree *free;
    GHashTable *properties;
    uint32_t ref;
    Object *parent;
};

/**
 * DECLARE_INSTANCE_CHECKER:
 * @InstanceType: instance struct name
 * @OBJ_NAME: the object name in uppercase with underscore separators
 * @TYPENAME: type name
 *
 * Direct usage of this macro should be avoided, and the complete
 * OBJECT_DECLARE_TYPE macro is recommended instead.
 *
 * This macro will provide the instance type cast functions for a
 * QOM type.
 */
#define DECLARE_INSTANCE_CHECKER(InstanceType, OBJ_NAME, TYPENAME) \
    static inline G_GNUC_UNUSED InstanceType * \
    OBJ_NAME(const void *obj) \
    { return OBJECT_CHECK(InstanceType, obj, TYPENAME); }

/**
 * DECLARE_CLASS_CHECKERS:
 * @ClassType: class struct name
 * @OBJ_NAME: the object name in uppercase with underscore separators
 * @TYPENAME: type name
 *
 * Direct usage of this macro should be avoided, and the complete
 * OBJECT_DECLARE_TYPE macro is recommended instead.
 *
 * This macro will provide the class type cast functions for a
 * QOM type.
 */
#define DECLARE_CLASS_CHECKERS(ClassType, OBJ_NAME, TYPENAME) \
    static inline G_GNUC_UNUSED ClassType * \
    OBJ_NAME##_GET_CLASS(const void *obj) \
    { return OBJECT_GET_CLASS(ClassType, obj, TYPENAME); } \
    \
    static inline G_GNUC_UNUSED ClassType * \
    OBJ_NAME##_CLASS(const void *klass) \
    { return OBJECT_CLASS_CHECK(ClassType, klass, TYPENAME); }

/**
 * DECLARE_OBJ_CHECKERS:
 * @InstanceType: instance struct name
 * @ClassType: class struct name
 * @OBJ_NAME: the object name in uppercase with underscore separators
 * @TYPENAME: type name
 *
 * Direct usage of this macro should be avoided, and the complete
 * OBJECT_DECLARE_TYPE macro is recommended instead.
 *
 * This macro will provide the three standard type cast functions for a
 * QOM type.
 */
#define DECLARE_OBJ_CHECKERS(InstanceType, ClassType, OBJ_NAME, TYPENAME) \
    DECLARE_INSTANCE_CHECKER(InstanceType, OBJ_NAME, TYPENAME) \
    \
    DECLARE_CLASS_CHECKERS(ClassType, OBJ_NAME, TYPENAME)

/**
 * OBJECT_DECLARE_TYPE:
 * @InstanceType: instance struct name
 * @ClassType: class struct name
 * @MODULE_OBJ_NAME: the object name in uppercase with underscore separators
 *
 * This macro is typically used in a header file, and will:
 *
 *   - create the typedefs for the object and class structs
 *   - register the type for use with g_autoptr
 *   - provide three standard type cast functions
 *
 * The object struct and class struct need to be declared manually.
 */
#define OBJECT_DECLARE_TYPE(InstanceType, ClassType, MODULE_OBJ_NAME) \
    typedef struct InstanceType InstanceType; \
    typedef struct ClassType ClassType; \
    \
    G_DEFINE_AUTOPTR_CLEANUP_FUNC(InstanceType, object_unref) \
    \
    DECLARE_OBJ_CHECKERS(InstanceType, ClassType, \
                         MODULE_OBJ_NAME, TYPE_##MODULE_OBJ_NAME)

/**
 * OBJECT_DECLARE_SIMPLE_TYPE:
 * @InstanceType: instance struct name
 * @MODULE_OBJ_NAME: the object name in uppercase with underscore separators
 *
 * This does the same as OBJECT_DECLARE_TYPE(), but with no class struct
 * declared.
 *
 * This macro should be used unless the class struct needs to have
 * virtual methods declared.
 */
#define OBJECT_DECLARE_SIMPLE_TYPE(InstanceType, MODULE_OBJ_NAME) \
    typedef struct InstanceType InstanceType; \
    \
    G_DEFINE_AUTOPTR_CLEANUP_FUNC(InstanceType, object_unref) \
    \
    DECLARE_INSTANCE_CHECKER(InstanceType, MODULE_OBJ_NAME, TYPE_##MODULE_OBJ_NAME)


/**
 * OBJECT_DEFINE_TYPE_EXTENDED:
 * @ModuleObjName: the object name with initial caps
 * @module_obj_name: the object name in lowercase with underscore separators
 * @MODULE_OBJ_NAME: the object name in uppercase with underscore separators
 * @PARENT_MODULE_OBJ_NAME: the parent object name in uppercase with underscore
 *                          separators
 * @ABSTRACT: boolean flag to indicate whether the object can be instantiated
 * @...: list of initializers for "InterfaceInfo" to declare implemented interfaces
 *
 * This macro is typically used in a source file, and will:
 *
 *   - declare prototypes for _finalize, _class_init and _init methods
 *   - declare the TypeInfo struct instance
 *   - provide the constructor to register the type
 *
 * After using this macro, implementations of the _finalize, _class_init,
 * and _init methods need to be written. Any of these can be zero-line
 * no-op impls if no special logic is required for a given type.
 *
 * This macro should rarely be used, instead one of the more specialized
 * macros is usually a better choice.
 */
#define OBJECT_DEFINE_TYPE_EXTENDED(ModuleObjName, module_obj_name, \
                                    MODULE_OBJ_NAME, PARENT_MODULE_OBJ_NAME, \
                                    ABSTRACT, ...) \
    static void \
    module_obj_name##_finalize(Object *obj); \
    static void \
    module_obj_name##_class_init(ObjectClass *oc, void *data); \
    static void \
    module_obj_name##_init(Object *obj); \
    \
    static const TypeInfo module_obj_name##_info = { \
        .parent = TYPE_##PARENT_MODULE_OBJ_NAME, \
        .name = TYPE_##MODULE_OBJ_NAME, \
        .instance_size = sizeof(ModuleObjName), \
        .instance_align = __alignof__(ModuleObjName), \
        .instance_init = module_obj_name##_init, \
        .instance_finalize = module_obj_name##_finalize, \
        .class_size = sizeof(ModuleObjName##Class), \
        .class_init = module_obj_name##_class_init, \
        .abstract = ABSTRACT, \
        .interfaces = (InterfaceInfo[]) { __VA_ARGS__ } , \
    }; \
    \
    static void \
    module_obj_name##_register_types(void) \
    { \
        type_register_static(&module_obj_name##_info); \
    } \
    type_init(module_obj_name##_register_types);

/**
 * OBJECT_DEFINE_TYPE:
 * @ModuleObjName: the object name with initial caps
 * @module_obj_name: the object name in lowercase with underscore separators
 * @MODULE_OBJ_NAME: the object name in uppercase with underscore separators
 * @PARENT_MODULE_OBJ_NAME: the parent object name in uppercase with underscore
 *                          separators
 *
 * This is a specialization of OBJECT_DEFINE_TYPE_EXTENDED, which is suitable
 * for the common case of a non-abstract type, without any interfaces.
 */
#define OBJECT_DEFINE_TYPE(ModuleObjName, module_obj_name, MODULE_OBJ_NAME, \
                           PARENT_MODULE_OBJ_NAME) \
    OBJECT_DEFINE_TYPE_EXTENDED(ModuleObjName, module_obj_name, \
                                MODULE_OBJ_NAME, PARENT_MODULE_OBJ_NAME, \
                                false, { NULL })

/**
 * OBJECT_DEFINE_TYPE_WITH_INTERFACES:
 * @ModuleObjName: the object name with initial caps
 * @module_obj_name: the object name in lowercase with underscore separators
 * @MODULE_OBJ_NAME: the object name in uppercase with underscore separators
 * @PARENT_MODULE_OBJ_NAME: the parent object name in uppercase with underscore
 *                          separators
 * @...: list of initializers for "InterfaceInfo" to declare implemented interfaces
 *
 * This is a specialization of OBJECT_DEFINE_TYPE_EXTENDED, which is suitable
 * for the common case of a non-abstract type, with one or more implemented
 * interfaces.
 *
 * Note when passing the list of interfaces, be sure to include the final
 * NULL entry, e.g.  { TYPE_USER_CREATABLE }, { NULL }
 */
#define OBJECT_DEFINE_TYPE_WITH_INTERFACES(ModuleObjName, module_obj_name, \
                                           MODULE_OBJ_NAME, \
                                           PARENT_MODULE_OBJ_NAME, ...) \
    OBJECT_DEFINE_TYPE_EXTENDED(ModuleObjName, module_obj_name, \
                                MODULE_OBJ_NAME, PARENT_MODULE_OBJ_NAME, \
                                false, __VA_ARGS__)

/**
 * OBJECT_DEFINE_ABSTRACT_TYPE:
 * @ModuleObjName: the object name with initial caps
 * @module_obj_name: the object name in lowercase with underscore separators
 * @MODULE_OBJ_NAME: the object name in uppercase with underscore separators
 * @PARENT_MODULE_OBJ_NAME: the parent object name in uppercase with underscore
 *                          separators
 *
 * This is a specialization of OBJECT_DEFINE_TYPE_EXTENDED, which is suitable
 * for defining an abstract type, without any interfaces.
 */
#define OBJECT_DEFINE_ABSTRACT_TYPE(ModuleObjName, module_obj_name, \
                                    MODULE_OBJ_NAME, PARENT_MODULE_OBJ_NAME) \
    OBJECT_DEFINE_TYPE_EXTENDED(ModuleObjName, module_obj_name, \
                                MODULE_OBJ_NAME, PARENT_MODULE_OBJ_NAME, \
                                true, { NULL })

/**
 * struct TypeInfo:
 * @name: The name of the type.
 * @parent: The name of the parent type.
 * @instance_size: The size of the object (derivative of #Object).  If
 *   @instance_size is 0, then the size of the object will be the size of the
 *   parent object.
 * @instance_align: The required alignment of the object.  If @instance_align
 *   is 0, then normal malloc alignment is sufficient; if non-zero, then we
 *   must use qemu_memalign for allocation.
 * @instance_init: This function is called to initialize an object.  The parent
 *   class will have already been initialized so the type is only responsible
 *   for initializing its own members.
 * @instance_post_init: This function is called to finish initialization of
 *   an object, after all @instance_init functions were called.
 * @instance_finalize: This function is called during object destruction.  This
 *   is called before the parent @instance_finalize function has been called.
 *   An object should only free the members that are unique to its type in this
 *   function.
 * @abstract: If this field is true, then the class is considered abstract and
 *   cannot be directly instantiated.
 * @class_size: The size of the class object (derivative of #ObjectClass)
 *   for this object.  If @class_size is 0, then the size of the class will be
 *   assumed to be the size of the parent class.  This allows a type to avoid
 *   implementing an explicit class type if they are not adding additional
 *   virtual functions.
 * @class_init: This function is called after all parent class initialization
 *   has occurred to allow a class to set its default virtual method pointers.
 *   This is also the function to use to override virtual methods from a parent
 *   class.
 * @class_base_init: This function is called for all base classes after all
 *   parent class initialization has occurred, but before the class itself
 *   is initialized.  This is the function to use to undo the effects of
 *   memcpy from the parent class to the descendants.
 * @class_data: Data to pass to the @class_init,
 *   @class_base_init. This can be useful when building dynamic
 *   classes.
 * @interfaces: The list of interfaces associated with this type.  This
 *   should point to a static array that's terminated with a zero filled
 *   element.
 */
struct TypeInfo
{
    const char *name;
    const char *parent;

    size_t instance_size;
    size_t instance_align;
    void (*instance_init)(Object *obj);
    void (*instance_post_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;
    size_t class_size;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_base_init)(ObjectClass *klass, void *data);
    void *class_data;

    InterfaceInfo *interfaces;
};

/**
 * OBJECT:
 * @obj: A derivative of #Object
 *
 * Converts an object to a #Object.  Since all objects are #Objects,
 * this function will always succeed.
 */
#define OBJECT(obj) \
    ((Object *)(obj))

/**
 * OBJECT_CLASS:
 * @class: A derivative of #ObjectClass.
 *
 * Converts a class to an #ObjectClass.  Since all objects are #Objects,
 * this function will always succeed.
 */
#define OBJECT_CLASS(class) \
    ((ObjectClass *)(class))

/**
 * OBJECT_CHECK:
 * @type: The C type to use for the return value.
 * @obj: A derivative of @type to cast.
 * @name: The QOM typename of @type
 *
 * A type safe version of @object_dynamic_cast_assert.  Typically each class
 * will define a macro based on this type to perform type safe dynamic_casts to
 * this object type.
 *
 * If an invalid object is passed to this function, a run time assert will be
 * generated.
 */
#define OBJECT_CHECK(type, obj, name) \
    ((type *)object_dynamic_cast_assert(OBJECT(obj), (name), \
                                        __FILE__, __LINE__, __func__))

/**
 * OBJECT_CLASS_CHECK:
 * @class_type: The C type to use for the return value.
 * @class: A derivative class of @class_type to cast.
 * @name: the QOM typename of @class_type.
 *
 * A type safe version of @object_class_dynamic_cast_assert.  This macro is
 * typically wrapped by each type to perform type safe casts of a class to a
 * specific class type.
 */
#define OBJECT_CLASS_CHECK(class_type, class, name) \
    ((class_type *)object_class_dynamic_cast_assert(OBJECT_CLASS(class), (name), \
                                               __FILE__, __LINE__, __func__))

/**
 * OBJECT_GET_CLASS:
 * @class: The C type to use for the return value.
 * @obj: The object to obtain the class for.
 * @name: The QOM typename of @obj.
 *
 * This function will return a specific class for a given object.  Its generally
 * used by each type to provide a type safe macro to get a specific class type
 * from an object.
 */
#define OBJECT_GET_CLASS(class, obj, name) \
    OBJECT_CLASS_CHECK(class, object_get_class(OBJECT(obj)), name)

/**
 * struct InterfaceInfo:
 * @type: The name of the interface.
 *
 * The information associated with an interface.
 */
struct InterfaceInfo {
    const char *type;
};

/**
 * struct InterfaceClass:
 * @parent_class: the base class
 *
 * The class for all interfaces.  Subclasses of this class should only add
 * virtual methods.
 */
struct InterfaceClass
{
    ObjectClass parent_class;
    /* private: */
    ObjectClass *concrete_class;
    Type interface_type;
};

#define TYPE_INTERFACE "interface"

/**
 * INTERFACE_CLASS:
 * @klass: class to cast from
 * Returns: An #InterfaceClass or raise an error if cast is invalid
 */
#define INTERFACE_CLASS(klass) \
    OBJECT_CLASS_CHECK(InterfaceClass, klass, TYPE_INTERFACE)

/**
 * INTERFACE_CHECK:
 * @interface: the type to return
 * @obj: the object to convert to an interface
 * @name: the interface type name
 *
 * Returns: @obj casted to @interface if cast is valid, otherwise raise error.
 */
#define INTERFACE_CHECK(interface, obj, name) \
    ((interface *)object_dynamic_cast_assert(OBJECT((obj)), (name), \
                                             __FILE__, __LINE__, __func__))

/**
 * object_new_with_class:
 * @klass: The class to instantiate.
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 *
 * Returns: The newly allocated and instantiated object.
 */
Object *object_new_with_class(ObjectClass *klass);

/**
 * object_new:
 * @typename: The name of the type of the object to instantiate.
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 *
 * Returns: The newly allocated and instantiated object.
 */
Object *object_new(const char *typename);

/**
 * object_new_with_props:
 * @typename:  The name of the type of the object to instantiate.
 * @parent: the parent object
 * @id: The unique ID of the object
 * @errp: pointer to error object
 * @...: list of property names and values
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 *
 * The @id parameter will be used when registering the object as a
 * child of @parent in the composition tree.
 *
 * The variadic parameters are a list of pairs of (propname, propvalue)
 * strings. The propname of %NULL indicates the end of the property
 * list. If the object implements the user creatable interface, the
 * object will be marked complete once all the properties have been
 * processed.
 *
 * .. code-block:: c
 *    :caption: Creating an object with properties
 *
 *      Error *err = NULL;
 *      Object *obj;
 *
 *      obj = object_new_with_props(TYPE_MEMORY_BACKEND_FILE,
 *                                  object_get_objects_root(),
 *                                  "hostmem0",
 *                                  &err,
 *                                  "share", "yes",
 *                                  "mem-path", "/dev/shm/somefile",
 *                                  "prealloc", "yes",
 *                                  "size", "1048576",
 *                                  NULL);
 *
 *      if (!obj) {
 *        error_reportf_err(err, "Cannot create memory backend: ");
 *      }
 *
 * The returned object will have one stable reference maintained
 * for as long as it is present in the object hierarchy.
 *
 * Returns: The newly allocated, instantiated & initialized object.
 */
Object *object_new_with_props(const char *typename,
                              Object *parent,
                              const char *id,
                              Error **errp,
                              ...) QEMU_SENTINEL;

/**
 * object_new_with_propv:
 * @typename:  The name of the type of the object to instantiate.
 * @parent: the parent object
 * @id: The unique ID of the object
 * @errp: pointer to error object
 * @vargs: list of property names and values
 *
 * See object_new_with_props() for documentation.
 */
Object *object_new_with_propv(const char *typename,
                              Object *parent,
                              const char *id,
                              Error **errp,
                              va_list vargs);

bool object_apply_global_props(Object *obj, const GPtrArray *props,
                               Error **errp);
void object_set_machine_compat_props(GPtrArray *compat_props);
void object_set_accelerator_compat_props(GPtrArray *compat_props);
void object_register_sugar_prop(const char *driver, const char *prop,
                                const char *value, bool optional);
void object_apply_compat_props(Object *obj);

/**
 * object_set_props:
 * @obj: the object instance to set properties on
 * @errp: pointer to error object
 * @...: list of property names and values
 *
 * This function will set a list of properties on an existing object
 * instance.
 *
 * The variadic parameters are a list of pairs of (propname, propvalue)
 * strings. The propname of %NULL indicates the end of the property
 * list.
 *
 * .. code-block:: c
 *    :caption: Update an object's properties
 *
 *      Error *err = NULL;
 *      Object *obj = ...get / create object...;
 *
 *      if (!object_set_props(obj,
 *                            &err,
 *                            "share", "yes",
 *                            "mem-path", "/dev/shm/somefile",
 *                            "prealloc", "yes",
 *                            "size", "1048576",
 *                            NULL)) {
 *        error_reportf_err(err, "Cannot set properties: ");
 *      }
 *
 * The returned object will have one stable reference maintained
 * for as long as it is present in the object hierarchy.
 *
 * Returns: %true on success, %false on error.
 */
bool object_set_props(Object *obj, Error **errp, ...) QEMU_SENTINEL;

/**
 * object_set_propv:
 * @obj: the object instance to set properties on
 * @errp: pointer to error object
 * @vargs: list of property names and values
 *
 * See object_set_props() for documentation.
 *
 * Returns: %true on success, %false on error.
 */
bool object_set_propv(Object *obj, Error **errp, va_list vargs);

/**
 * object_initialize:
 * @obj: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @obj for the object.
 * @typename: The name of the type of the object to instantiate.
 *
 * This function will initialize an object.  The memory for the object should
 * have already been allocated.  The returned object has a reference count of 1,
 * and will be finalized when the last reference is dropped.
 */
void object_initialize(void *obj, size_t size, const char *typename);

/**
 * object_initialize_child_with_props:
 * @parentobj: The parent object to add a property to
 * @propname: The name of the property
 * @childobj: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @childobj for the object.
 * @type: The name of the type of the object to instantiate.
 * @errp: If an error occurs, a pointer to an area to store the error
 * @...: list of property names and values
 *
 * This function will initialize an object. The memory for the object should
 * have already been allocated. The object will then be added as child property
 * to a parent with object_property_add_child() function. The returned object
 * has a reference count of 1 (for the "child<...>" property from the parent),
 * so the object will be finalized automatically when the parent gets removed.
 *
 * The variadic parameters are a list of pairs of (propname, propvalue)
 * strings. The propname of %NULL indicates the end of the property list.
 * If the object implements the user creatable interface, the object will
 * be marked complete once all the properties have been processed.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_initialize_child_with_props(Object *parentobj,
                             const char *propname,
                             void *childobj, size_t size, const char *type,
                             Error **errp, ...) QEMU_SENTINEL;

/**
 * object_initialize_child_with_propsv:
 * @parentobj: The parent object to add a property to
 * @propname: The name of the property
 * @childobj: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @childobj for the object.
 * @type: The name of the type of the object to instantiate.
 * @errp: If an error occurs, a pointer to an area to store the error
 * @vargs: list of property names and values
 *
 * See object_initialize_child() for documentation.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_initialize_child_with_propsv(Object *parentobj,
                              const char *propname,
                              void *childobj, size_t size, const char *type,
                              Error **errp, va_list vargs);

/**
 * object_initialize_child:
 * @parent: The parent object to add a property to
 * @propname: The name of the property
 * @child: A precisely typed pointer to the memory to be used for the
 * object.
 * @type: The name of the type of the object to instantiate.
 *
 * This is like::
 *
 *   object_initialize_child_with_props(parent, propname,
 *                                      child, sizeof(*child), type,
 *                                      &error_abort, NULL)
 */
#define object_initialize_child(parent, propname, child, type)          \
    object_initialize_child_internal((parent), (propname),              \
                                     (child), sizeof(*(child)), (type))
void object_initialize_child_internal(Object *parent, const char *propname,
                                      void *child, size_t size,
                                      const char *type);

/**
 * object_dynamic_cast:
 * @obj: The object to cast.
 * @typename: The @typename to cast to.
 *
 * This function will determine if @obj is-a @typename.  @obj can refer to an
 * object or an interface associated with an object.
 *
 * Returns: This function returns @obj on success or #NULL on failure.
 */
Object *object_dynamic_cast(Object *obj, const char *typename);

/**
 * object_dynamic_cast_assert:
 * @obj: The object to cast.
 * @typename: The @typename to cast to.
 * @file: Source code file where function was called
 * @line: Source code line where function was called
 * @func: Name of function where this function was called
 *
 * See object_dynamic_cast() for a description of the parameters of this
 * function.  The only difference in behavior is that this function asserts
 * instead of returning #NULL on failure if QOM cast debugging is enabled.
 * This function is not meant to be called directly, but only through
 * the wrapper macro OBJECT_CHECK.
 */
Object *object_dynamic_cast_assert(Object *obj, const char *typename,
                                   const char *file, int line, const char *func);

/**
 * object_get_class:
 * @obj: A derivative of #Object
 *
 * Returns: The #ObjectClass of the type associated with @obj.
 */
ObjectClass *object_get_class(Object *obj);

/**
 * object_get_typename:
 * @obj: A derivative of #Object.
 *
 * Returns: The QOM typename of @obj.
 */
const char *object_get_typename(const Object *obj);

/**
 * type_register_static:
 * @info: The #TypeInfo of the new type.
 *
 * @info and all of the strings it points to should exist for the life time
 * that the type is registered.
 *
 * Returns: the new #Type.
 */
Type type_register_static(const TypeInfo *info);

/**
 * type_register:
 * @info: The #TypeInfo of the new type
 *
 * Unlike type_register_static(), this call does not require @info or its
 * string members to continue to exist after the call returns.
 *
 * Returns: the new #Type.
 */
Type type_register(const TypeInfo *info);

/**
 * type_register_static_array:
 * @infos: The array of the new type #TypeInfo structures.
 * @nr_infos: number of entries in @infos
 *
 * @infos and all of the strings it points to should exist for the life time
 * that the type is registered.
 */
void type_register_static_array(const TypeInfo *infos, int nr_infos);

/**
 * DEFINE_TYPES:
 * @type_array: The array containing #TypeInfo structures to register
 *
 * @type_array should be static constant that exists for the life time
 * that the type is registered.
 */
#define DEFINE_TYPES(type_array)                                            \
static void do_qemu_init_ ## type_array(void)                               \
{                                                                           \
    type_register_static_array(type_array, ARRAY_SIZE(type_array));         \
}                                                                           \
type_init(do_qemu_init_ ## type_array)

/**
 * type_print_class_properties:
 * @type: a QOM class name
 *
 * Print the object's class properties to stdout or the monitor.
 * Return whether an object was found.
 */
bool type_print_class_properties(const char *type);

/**
 * object_set_properties_from_keyval:
 * @obj: a QOM object
 * @qdict: a dictionary with the properties to be set
 * @from_json: true if leaf values of @qdict are typed, false if they
 * are strings
 * @errp: pointer to error object
 *
 * For each key in the dictionary, parse the value string if needed,
 * then set the corresponding property in @obj.
 */
void object_set_properties_from_keyval(Object *obj, const QDict *qdict,
                                       bool from_json, Error **errp);

/**
 * object_class_dynamic_cast_assert:
 * @klass: The #ObjectClass to attempt to cast.
 * @typename: The QOM typename of the class to cast to.
 * @file: Source code file where function was called
 * @line: Source code line where function was called
 * @func: Name of function where this function was called
 *
 * See object_class_dynamic_cast() for a description of the parameters
 * of this function.  The only difference in behavior is that this function
 * asserts instead of returning #NULL on failure if QOM cast debugging is
 * enabled.  This function is not meant to be called directly, but only through
 * the wrapper macro OBJECT_CLASS_CHECK.
 */
ObjectClass *object_class_dynamic_cast_assert(ObjectClass *klass,
                                              const char *typename,
                                              const char *file, int line,
                                              const char *func);

/**
 * object_class_dynamic_cast:
 * @klass: The #ObjectClass to attempt to cast.
 * @typename: The QOM typename of the class to cast to.
 *
 * Returns: If @typename is a class, this function returns @klass if
 * @typename is a subtype of @klass, else returns #NULL.
 *
 * If @typename is an interface, this function returns the interface
 * definition for @klass if @klass implements it unambiguously; #NULL
 * is returned if @klass does not implement the interface or if multiple
 * classes or interfaces on the hierarchy leading to @klass implement
 * it.  (FIXME: perhaps this can be detected at type definition time?)
 */
ObjectClass *object_class_dynamic_cast(ObjectClass *klass,
                                       const char *typename);

/**
 * object_class_get_parent:
 * @klass: The class to obtain the parent for.
 *
 * Returns: The parent for @klass or %NULL if none.
 */
ObjectClass *object_class_get_parent(ObjectClass *klass);

/**
 * object_class_get_name:
 * @klass: The class to obtain the QOM typename for.
 *
 * Returns: The QOM typename for @klass.
 */
const char *object_class_get_name(ObjectClass *klass);

/**
 * object_class_is_abstract:
 * @klass: The class to obtain the abstractness for.
 *
 * Returns: %true if @klass is abstract, %false otherwise.
 */
bool object_class_is_abstract(ObjectClass *klass);

/**
 * object_class_by_name:
 * @typename: The QOM typename to obtain the class for.
 *
 * Returns: The class for @typename or %NULL if not found.
 */
ObjectClass *object_class_by_name(const char *typename);

/**
 * module_object_class_by_name:
 * @typename: The QOM typename to obtain the class for.
 *
 * For objects which might be provided by a module.  Behaves like
 * object_class_by_name, but additionally tries to load the module
 * needed in case the class is not available.
 *
 * Returns: The class for @typename or %NULL if not found.
 */
ObjectClass *module_object_class_by_name(const char *typename);

void object_class_foreach(void (*fn)(ObjectClass *klass, void *opaque),
                          const char *implements_type, bool include_abstract,
                          void *opaque);

/**
 * object_class_get_list:
 * @implements_type: The type to filter for, including its derivatives.
 * @include_abstract: Whether to include abstract classes.
 *
 * Returns: A singly-linked list of the classes in reverse hashtable order.
 */
GSList *object_class_get_list(const char *implements_type,
                              bool include_abstract);

/**
 * object_class_get_list_sorted:
 * @implements_type: The type to filter for, including its derivatives.
 * @include_abstract: Whether to include abstract classes.
 *
 * Returns: A singly-linked list of the classes in alphabetical
 * case-insensitive order.
 */
GSList *object_class_get_list_sorted(const char *implements_type,
                              bool include_abstract);

/**
 * object_ref:
 * @obj: the object
 *
 * Increase the reference count of a object.  A object cannot be freed as long
 * as its reference count is greater than zero.
 * Returns: @obj
 */
Object *object_ref(void *obj);

/**
 * object_unref:
 * @obj: the object
 *
 * Decrease the reference count of a object.  A object cannot be freed as long
 * as its reference count is greater than zero.
 */
void object_unref(void *obj);

/**
 * object_property_try_add:
 * @obj: the object to add a property to
 * @name: the name of the property.  This can contain any character except for
 *  a forward slash.  In general, you should use hyphens '-' instead of
 *  underscores '_' when naming properties.
 * @type: the type name of the property.  This namespace is pretty loosely
 *   defined.  Sub namespaces are constructed by using a prefix and then
 *   to angle brackets.  For instance, the type 'virtio-net-pci' in the
 *   'link' namespace would be 'link<virtio-net-pci>'.
 * @get: The getter to be called to read a property.  If this is NULL, then
 *   the property cannot be read.
 * @set: the setter to be called to write a property.  If this is NULL,
 *   then the property cannot be written.
 * @release: called when the property is removed from the object.  This is
 *   meant to allow a property to free its opaque upon object
 *   destruction.  This may be NULL.
 * @opaque: an opaque pointer to pass to the callbacks for the property
 * @errp: pointer to error object
 *
 * Returns: The #ObjectProperty; this can be used to set the @resolve
 * callback for child and link properties.
 */
ObjectProperty *object_property_try_add(Object *obj, const char *name,
                                        const char *type,
                                        ObjectPropertyAccessor *get,
                                        ObjectPropertyAccessor *set,
                                        ObjectPropertyRelease *release,
                                        void *opaque, Error **errp);

/**
 * object_property_add:
 * Same as object_property_try_add() with @errp hardcoded to
 * &error_abort.
 *
 * @obj: the object to add a property to
 * @name: the name of the property.  This can contain any character except for
 *  a forward slash.  In general, you should use hyphens '-' instead of
 *  underscores '_' when naming properties.
 * @type: the type name of the property.  This namespace is pretty loosely
 *   defined.  Sub namespaces are constructed by using a prefix and then
 *   to angle brackets.  For instance, the type 'virtio-net-pci' in the
 *   'link' namespace would be 'link<virtio-net-pci>'.
 * @get: The getter to be called to read a property.  If this is NULL, then
 *   the property cannot be read.
 * @set: the setter to be called to write a property.  If this is NULL,
 *   then the property cannot be written.
 * @release: called when the property is removed from the object.  This is
 *   meant to allow a property to free its opaque upon object
 *   destruction.  This may be NULL.
 * @opaque: an opaque pointer to pass to the callbacks for the property
 */
ObjectProperty *object_property_add(Object *obj, const char *name,
                                    const char *type,
                                    ObjectPropertyAccessor *get,
                                    ObjectPropertyAccessor *set,
                                    ObjectPropertyRelease *release,
                                    void *opaque);

void object_property_del(Object *obj, const char *name);

ObjectProperty *object_class_property_add(ObjectClass *klass, const char *name,
                                          const char *type,
                                          ObjectPropertyAccessor *get,
                                          ObjectPropertyAccessor *set,
                                          ObjectPropertyRelease *release,
                                          void *opaque);

/**
 * object_property_set_default_bool:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.
 */
void object_property_set_default_bool(ObjectProperty *prop, bool value);

/**
 * object_property_set_default_str:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.
 */
void object_property_set_default_str(ObjectProperty *prop, const char *value);

/**
 * object_property_set_default_int:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.
 */
void object_property_set_default_int(ObjectProperty *prop, int64_t value);

/**
 * object_property_set_default_uint:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.
 */
void object_property_set_default_uint(ObjectProperty *prop, uint64_t value);

/**
 * object_property_find:
 * @obj: the object
 * @name: the name of the property
 *
 * Look up a property for an object.
 *
 * Return its #ObjectProperty if found, or NULL.
 */
ObjectProperty *object_property_find(Object *obj, const char *name);

/**
 * object_property_find_err:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Look up a property for an object.
 *
 * Return its #ObjectProperty if found, or NULL.
 */
ObjectProperty *object_property_find_err(Object *obj,
                                         const char *name,
                                         Error **errp);

/**
 * object_class_property_find:
 * @klass: the object class
 * @name: the name of the property
 *
 * Look up a property for an object class.
 *
 * Return its #ObjectProperty if found, or NULL.
 */
ObjectProperty *object_class_property_find(ObjectClass *klass,
                                           const char *name);

/**
 * object_class_property_find_err:
 * @klass: the object class
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Look up a property for an object class.
 *
 * Return its #ObjectProperty if found, or NULL.
 */
ObjectProperty *object_class_property_find_err(ObjectClass *klass,
                                               const char *name,
                                               Error **errp);

typedef struct ObjectPropertyIterator {
    ObjectClass *nextclass;
    GHashTableIter iter;
} ObjectPropertyIterator;

/**
 * object_property_iter_init:
 * @iter: the iterator instance
 * @obj: the object
 *
 * Initializes an iterator for traversing all properties
 * registered against an object instance, its class and all parent classes.
 *
 * It is forbidden to modify the property list while iterating,
 * whether removing or adding properties.
 *
 * Typical usage pattern would be
 *
 * .. code-block:: c
 *    :caption: Using object property iterators
 *
 *      ObjectProperty *prop;
 *      ObjectPropertyIterator iter;
 *
 *      object_property_iter_init(&iter, obj);
 *      while ((prop = object_property_iter_next(&iter))) {
 *        ... do something with prop ...
 *      }
 */
void object_property_iter_init(ObjectPropertyIterator *iter,
                               Object *obj);

/**
 * object_class_property_iter_init:
 * @iter: the iterator instance
 * @klass: the class
 *
 * Initializes an iterator for traversing all properties
 * registered against an object class and all parent classes.
 *
 * It is forbidden to modify the property list while iterating,
 * whether removing or adding properties.
 *
 * This can be used on abstract classes as it does not create a temporary
 * instance.
 */
void object_class_property_iter_init(ObjectPropertyIterator *iter,
                                     ObjectClass *klass);

/**
 * object_property_iter_next:
 * @iter: the iterator instance
 *
 * Return the next available property. If no further properties
 * are available, a %NULL value will be returned and the @iter
 * pointer should not be used again after this point without
 * re-initializing it.
 *
 * Returns: the next property, or %NULL when all properties
 * have been traversed.
 */
ObjectProperty *object_property_iter_next(ObjectPropertyIterator *iter);

void object_unparent(Object *obj);

/**
 * object_property_get:
 * @obj: the object
 * @name: the name of the property
 * @v: the visitor that will receive the property value.  This should be an
 *   Output visitor and the data will be written with @name as the name.
 * @errp: returns an error if this function fails
 *
 * Reads a property from a object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_get(Object *obj, const char *name, Visitor *v,
                         Error **errp);

/**
 * object_property_set_str:
 * @obj: the object
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes a string value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_str(Object *obj, const char *name,
                             const char *value, Error **errp);

/**
 * object_property_get_str:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to a C string, or NULL if
 * an error occurs (including when the property value is not a string).
 * The caller should free the string.
 */
char *object_property_get_str(Object *obj, const char *name,
                              Error **errp);

/**
 * object_property_set_link:
 * @obj: the object
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes an object's canonical path to a property.
 *
 * If the link property was created with
 * %OBJ_PROP_LINK_STRONG bit, the old target object is
 * unreferenced, and a reference is added to the new target object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_link(Object *obj, const char *name,
                              Object *value, Error **errp);

/**
 * object_property_get_link:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, resolved from a path to an Object,
 * or NULL if an error occurs (including when the property value is not a
 * string or not a valid object path).
 */
Object *object_property_get_link(Object *obj, const char *name,
                                 Error **errp);

/**
 * object_property_set_bool:
 * @obj: the object
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes a bool value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_bool(Object *obj, const char *name,
                              bool value, Error **errp);

/**
 * object_property_get_bool:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to a boolean, or false if
 * an error occurs (including when the property value is not a bool).
 */
bool object_property_get_bool(Object *obj, const char *name,
                              Error **errp);

/**
 * object_property_set_int:
 * @obj: the object
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes an integer value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_int(Object *obj, const char *name,
                             int64_t value, Error **errp);

/**
 * object_property_get_int:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an integer, or -1 if
 * an error occurs (including when the property value is not an integer).
 */
int64_t object_property_get_int(Object *obj, const char *name,
                                Error **errp);

/**
 * object_property_set_uint:
 * @obj: the object
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes an unsigned integer value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_uint(Object *obj, const char *name,
                              uint64_t value, Error **errp);

/**
 * object_property_get_uint:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an unsigned integer, or 0
 * an error occurs (including when the property value is not an integer).
 */
uint64_t object_property_get_uint(Object *obj, const char *name,
                                  Error **errp);

/**
 * object_property_get_enum:
 * @obj: the object
 * @name: the name of the property
 * @typename: the name of the enum data type
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an integer (which
 * can't be negative), or -1 on error (including when the property
 * value is not an enum).
 */
int object_property_get_enum(Object *obj, const char *name,
                             const char *typename, Error **errp);

/**
 * object_property_set:
 * @obj: the object
 * @name: the name of the property
 * @v: the visitor that will be used to write the property value.  This should
 *   be an Input visitor and the data will be first read with @name as the
 *   name and then written as the property value.
 * @errp: returns an error if this function fails
 *
 * Writes a property to a object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set(Object *obj, const char *name, Visitor *v,
                         Error **errp);

/**
 * object_property_parse:
 * @obj: the object
 * @name: the name of the property
 * @string: the string that will be used to parse the property value.
 * @errp: returns an error if this function fails
 *
 * Parses a string and writes the result into a property of an object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_parse(Object *obj, const char *name,
                           const char *string, Error **errp);

/**
 * object_property_print:
 * @obj: the object
 * @name: the name of the property
 * @human: if true, print for human consumption
 * @errp: returns an error if this function fails
 *
 * Returns a string representation of the value of the property.  The
 * caller shall free the string.
 */
char *object_property_print(Object *obj, const char *name, bool human,
                            Error **errp);

/**
 * object_property_get_type:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns:  The type name of the property.
 */
const char *object_property_get_type(Object *obj, const char *name,
                                     Error **errp);

/**
 * object_get_root:
 *
 * Returns: the root object of the composition tree
 */
Object *object_get_root(void);


/**
 * object_get_objects_root:
 *
 * Get the container object that holds user created
 * object instances. This is the object at path
 * "/objects"
 *
 * Returns: the user object container
 */
Object *object_get_objects_root(void);

/**
 * object_get_internal_root:
 *
 * Get the container object that holds internally used object
 * instances.  Any object which is put into this container must not be
 * user visible, and it will not be exposed in the QOM tree.
 *
 * Returns: the internal object container
 */
Object *object_get_internal_root(void);

/**
 * object_get_canonical_path_component:
 * @obj: the object
 *
 * Returns: The final component in the object's canonical path.  The canonical
 * path is the path within the composition tree starting from the root.
 * %NULL if the object doesn't have a parent (and thus a canonical path).
 */
const char *object_get_canonical_path_component(const Object *obj);

/**
 * object_get_canonical_path:
 * @obj: the object
 *
 * Returns: The canonical path for a object, newly allocated.  This is
 * the path within the composition tree starting from the root.  Use
 * g_free() to free it.
 */
char *object_get_canonical_path(const Object *obj);

/**
 * object_resolve_path:
 * @path: the path to resolve
 * @ambiguous: returns true if the path resolution failed because of an
 *   ambiguous match
 *
 * There are two types of supported paths--absolute paths and partial paths.
 * 
 * Absolute paths are derived from the root object and can follow child<> or
 * link<> properties.  Since they can follow link<> properties, they can be
 * arbitrarily long.  Absolute paths look like absolute filenames and are
 * prefixed with a leading slash.
 * 
 * Partial paths look like relative filenames.  They do not begin with a
 * prefix.  The matching rules for partial paths are subtle but designed to make
 * specifying objects easy.  At each level of the composition tree, the partial
 * path is matched as an absolute path.  The first match is not returned.  At
 * least two matches are searched for.  A successful result is only returned if
 * only one match is found.  If more than one match is found, a flag is
 * returned to indicate that the match was ambiguous.
 *
 * Returns: The matched object or NULL on path lookup failure.
 */
Object *object_resolve_path(const char *path, bool *ambiguous);

/**
 * object_resolve_path_type:
 * @path: the path to resolve
 * @typename: the type to look for.
 * @ambiguous: returns true if the path resolution failed because of an
 *   ambiguous match
 *
 * This is similar to object_resolve_path.  However, when looking for a
 * partial path only matches that implement the given type are considered.
 * This restricts the search and avoids spuriously flagging matches as
 * ambiguous.
 *
 * For both partial and absolute paths, the return value goes through
 * a dynamic cast to @typename.  This is important if either the link,
 * or the typename itself are of interface types.
 *
 * Returns: The matched object or NULL on path lookup failure.
 */
Object *object_resolve_path_type(const char *path, const char *typename,
                                 bool *ambiguous);

/**
 * object_resolve_path_at:
 * @parent: the object in which to resolve the path
 * @path: the path to resolve
 *
 * This is like object_resolve_path(), except paths not starting with
 * a slash are relative to @parent.
 *
 * Returns: The resolved object or NULL on path lookup failure.
 */
Object *object_resolve_path_at(Object *parent, const char *path);

/**
 * object_resolve_path_component:
 * @parent: the object in which to resolve the path
 * @part: the component to resolve.
 *
 * This is similar to object_resolve_path with an absolute path, but it
 * only resolves one element (@part) and takes the others from @parent.
 *
 * Returns: The resolved object or NULL on path lookup failure.
 */
Object *object_resolve_path_component(Object *parent, const char *part);

/**
 * object_property_try_add_child:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @child: the child object
 * @errp: pointer to error object
 *
 * Child properties form the composition tree.  All objects need to be a child
 * of another object.  Objects can only be a child of one object.
 *
 * There is no way for a child to determine what its parent is.  It is not
 * a bidirectional relationship.  This is by design.
 *
 * The value of a child property as a C string will be the child object's
 * canonical path. It can be retrieved using object_property_get_str().
 * The child object itself can be retrieved using object_property_get_link().
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_try_add_child(Object *obj, const char *name,
                                              Object *child, Error **errp);

/**
 * object_property_add_child:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @child: the child object
 *
 * Same as object_property_try_add_child() with @errp hardcoded to
 * &error_abort
 */
ObjectProperty *object_property_add_child(Object *obj, const char *name,
                                          Object *child);

typedef enum {
    /* Unref the link pointer when the property is deleted */
    OBJ_PROP_LINK_STRONG = 0x1,

    /* private */
    OBJ_PROP_LINK_DIRECT = 0x2,
    OBJ_PROP_LINK_CLASS = 0x4,
} ObjectPropertyLinkFlags;

/**
 * object_property_allow_set_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @child: the child object
 * @errp: pointer to error object
 *
 * The default implementation of the object_property_add_link() check()
 * callback function.  It allows the link property to be set and never returns
 * an error.
 */
void object_property_allow_set_link(const Object *obj, const char *name,
                                    Object *child, Error **errp);

/**
 * object_property_add_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @type: the qobj type of the link
 * @targetp: a pointer to where the link object reference is stored
 * @check: callback to veto setting or NULL if the property is read-only
 * @flags: additional options for the link
 *
 * Links establish relationships between objects.  Links are unidirectional
 * although two links can be combined to form a bidirectional relationship
 * between objects.
 *
 * Links form the graph in the object model.
 *
 * The @check() callback is invoked when
 * object_property_set_link() is called and can raise an error to prevent the
 * link being set.  If @check is NULL, the property is read-only
 * and cannot be set.
 *
 * Ownership of the pointer that @child points to is transferred to the
 * link property.  The reference count for *@child is
 * managed by the property from after the function returns till the
 * property is deleted with object_property_del().  If the
 * @flags %OBJ_PROP_LINK_STRONG bit is set,
 * the reference count is decremented when the property is deleted or
 * modified.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_link(Object *obj, const char *name,
                              const char *type, Object **targetp,
                              void (*check)(const Object *obj, const char *name,
                                            Object *val, Error **errp),
                              ObjectPropertyLinkFlags flags);

ObjectProperty *object_class_property_add_link(ObjectClass *oc,
                              const char *name,
                              const char *type, ptrdiff_t offset,
                              void (*check)(const Object *obj, const char *name,
                                            Object *val, Error **errp),
                              ObjectPropertyLinkFlags flags);

/**
 * object_property_add_str:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.  This function must
 *   return a string to be freed by g_free().
 * @set: the setter or NULL if the property is read-only
 *
 * Add a string property using getters/setters.  This function will add a
 * property of type 'string'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_str(Object *obj, const char *name,
                             char *(*get)(Object *, Error **),
                             void (*set)(Object *, const char *, Error **));

ObjectProperty *object_class_property_add_str(ObjectClass *klass,
                                   const char *name,
                                   char *(*get)(Object *, Error **),
                                   void (*set)(Object *, const char *,
                                               Error **));

/**
 * object_property_add_bool:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.
 * @set: the setter or NULL if the property is read-only
 *
 * Add a bool property using getters/setters.  This function will add a
 * property of type 'bool'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_bool(Object *obj, const char *name,
                              bool (*get)(Object *, Error **),
                              void (*set)(Object *, bool, Error **));

ObjectProperty *object_class_property_add_bool(ObjectClass *klass,
                                    const char *name,
                                    bool (*get)(Object *, Error **),
                                    void (*set)(Object *, bool, Error **));

/**
 * object_property_add_enum:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @typename: the name of the enum data type
 * @lookup: enum value namelookup table
 * @get: the getter or %NULL if the property is write-only.
 * @set: the setter or %NULL if the property is read-only
 *
 * Add an enum property using getters/setters.  This function will add a
 * property of type '@typename'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_enum(Object *obj, const char *name,
                              const char *typename,
                              const QEnumLookup *lookup,
                              int (*get)(Object *, Error **),
                              void (*set)(Object *, int, Error **));

ObjectProperty *object_class_property_add_enum(ObjectClass *klass,
                                    const char *name,
                                    const char *typename,
                                    const QEnumLookup *lookup,
                                    int (*get)(Object *, Error **),
                                    void (*set)(Object *, int, Error **));

/**
 * object_property_add_tm:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.
 *
 * Add a read-only struct tm valued property using a getter function.
 * This function will add a property of type 'struct tm'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_tm(Object *obj, const char *name,
                            void (*get)(Object *, struct tm *, Error **));

ObjectProperty *object_class_property_add_tm(ObjectClass *klass,
                            const char *name,
                            void (*get)(Object *, struct tm *, Error **));

typedef enum {
    /* Automatically add a getter to the property */
    OBJ_PROP_FLAG_READ = 1 << 0,
    /* Automatically add a setter to the property */
    OBJ_PROP_FLAG_WRITE = 1 << 1,
    /* Automatically add a getter and a setter to the property */
    OBJ_PROP_FLAG_READWRITE = (OBJ_PROP_FLAG_READ | OBJ_PROP_FLAG_WRITE),
} ObjectPropertyFlags;

/**
 * object_property_add_uint8_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint8'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint8_ptr(Object *obj, const char *name,
                                              const uint8_t *v,
                                              ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint8_ptr(ObjectClass *klass,
                                         const char *name,
                                         const uint8_t *v,
                                         ObjectPropertyFlags flags);

/**
 * object_property_add_uint16_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint16'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint16_ptr(Object *obj, const char *name,
                                    const uint16_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint16_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint16_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_uint32_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint32'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint32_ptr(Object *obj, const char *name,
                                    const uint32_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint32_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint32_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_uint64_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint64'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint64_ptr(Object *obj, const char *name,
                                    const uint64_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint64_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint64_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_alias:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @target_obj: the object to forward property access to
 * @target_name: the name of the property on the forwarded object
 *
 * Add an alias for a property on an object.  This function will add a property
 * of the same type as the forwarded property.
 *
 * The caller must ensure that @target_obj stays alive as long as
 * this property exists.  In the case of a child object or an alias on the same
 * object this will be the case.  For aliases to other objects the caller is
 * responsible for taking a reference.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_alias(Object *obj, const char *name,
                               Object *target_obj, const char *target_name);

/**
 * object_property_add_const_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @target: the object to be referred by the link
 *
 * Add an unmodifiable link for a property on an object.  This function will
 * add a property of type link<TYPE> where TYPE is the type of @target.
 *
 * The caller must ensure that @target stays alive as long as
 * this property exists.  In the case @target is a child of @obj,
 * this will be the case.  Otherwise, the caller is responsible for
 * taking a reference.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_const_link(Object *obj, const char *name,
                                               Object *target);

/**
 * object_property_set_description:
 * @obj: the object owning the property
 * @name: the name of the property
 * @description: the description of the property on the object
 *
 * Set an object property's description.
 *
 * Returns: %true on success, %false on failure.
 */
void object_property_set_description(Object *obj, const char *name,
                                     const char *description);
void object_class_property_set_description(ObjectClass *klass, const char *name,
                                           const char *description);

/**
 * object_child_foreach:
 * @obj: the object whose children will be navigated
 * @fn: the iterator function to be called
 * @opaque: an opaque value that will be passed to the iterator
 *
 * Call @fn passing each child of @obj and @opaque to it, until @fn returns
 * non-zero.
 *
 * It is forbidden to add or remove children from @obj from the @fn
 * callback.
 *
 * Returns: The last value returned by @fn, or 0 if there is no child.
 */
int object_child_foreach(Object *obj, int (*fn)(Object *child, void *opaque),
                         void *opaque);

/**
 * object_child_foreach_recursive:
 * @obj: the object whose children will be navigated
 * @fn: the iterator function to be called
 * @opaque: an opaque value that will be passed to the iterator
 *
 * Call @fn passing each child of @obj and @opaque to it, until @fn returns
 * non-zero. Calls recursively, all child nodes of @obj will also be passed
 * all the way down to the leaf nodes of the tree. Depth first ordering.
 *
 * It is forbidden to add or remove children from @obj (or its
 * child nodes) from the @fn callback.
 *
 * Returns: The last value returned by @fn, or 0 if there is no child.
 */
int object_child_foreach_recursive(Object *obj,
                                   int (*fn)(Object *child, void *opaque),
                                   void *opaque);
/**
 * container_get:
 * @root: root of the #path, e.g., object_get_root()
 * @path: path to the container
 *
 * Return a container object whose path is @path.  Create more containers
 * along the path if necessary.
 *
 * Returns: the container object.
 */
Object *container_get(Object *root, const char *path);

/**
 * object_type_get_instance_size:
 * @typename: Name of the Type whose instance_size is required
 *
 * Returns the instance_size of the given @typename.
 */
size_t object_type_get_instance_size(const char *typename);

/**
 * object_property_help:
 * @name: the name of the property
 * @type: the type of the property
 * @defval: the default value
 * @description: description of the property
 *
 * Returns: a user-friendly formatted string describing the property
 * for help purposes.
 */
char *object_property_help(const char *name, const char *type,
                           QObject *defval, const char *description);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Object, object_unref)

#endif
