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

#include "qapi-types.h"
#include "qemu/queue.h"

struct TypeImpl;
typedef struct TypeImpl *Type;

typedef struct ObjectClass ObjectClass;
typedef struct Object Object;

typedef struct TypeInfo TypeInfo;

typedef struct InterfaceClass InterfaceClass;
typedef struct InterfaceInfo InterfaceInfo;

#define TYPE_OBJECT "object"

/**
 * SECTION:object.h
 * @title:Base Object Type System
 * @short_description: interfaces for creating new types and objects
 *
 * The QEMU Object Model provides a framework for registering user creatable
 * types and instantiating objects from those types.  QOM provides the following
 * features:
 *
 *  - System for dynamically registering types
 *  - Support for single-inheritance of types
 *  - Multiple inheritance of stateless interfaces
 *
 * <example>
 *   <title>Creating a minimal type</title>
 *   <programlisting>
 * #include "qdev.h"
 *
 * #define TYPE_MY_DEVICE "my-device"
 *
 * // No new virtual functions: we can reuse the typedef for the
 * // superclass.
 * typedef DeviceClass MyDeviceClass;
 * typedef struct MyDevice
 * {
 *     DeviceState parent;
 *
 *     int reg0, reg1, reg2;
 * } MyDevice;
 *
 * static const TypeInfo my_device_info = {
 *     .name = TYPE_MY_DEVICE,
 *     .parent = TYPE_DEVICE,
 *     .instance_size = sizeof(MyDevice),
 * };
 *
 * static void my_device_register_types(void)
 * {
 *     type_register_static(&my_device_info);
 * }
 *
 * type_init(my_device_register_types)
 *   </programlisting>
 * </example>
 *
 * In the above example, we create a simple type that is described by #TypeInfo.
 * #TypeInfo describes information about the type including what it inherits
 * from, the instance and class size, and constructor/destructor hooks.
 *
 * Every type has an #ObjectClass associated with it.  #ObjectClass derivatives
 * are instantiated dynamically but there is only ever one instance for any
 * given type.  The #ObjectClass typically holds a table of function pointers
 * for the virtual methods implemented by this type.
 *
 * Using object_new(), a new #Object derivative will be instantiated.  You can
 * cast an #Object to a subclass (or base-class) type using
 * object_dynamic_cast().  You typically want to define macro wrappers around
 * OBJECT_CHECK() and OBJECT_CLASS_CHECK() to make it easier to convert to a
 * specific type:
 *
 * <example>
 *   <title>Typecasting macros</title>
 *   <programlisting>
 *    #define MY_DEVICE_GET_CLASS(obj) \
 *       OBJECT_GET_CLASS(MyDeviceClass, obj, TYPE_MY_DEVICE)
 *    #define MY_DEVICE_CLASS(klass) \
 *       OBJECT_CLASS_CHECK(MyDeviceClass, klass, TYPE_MY_DEVICE)
 *    #define MY_DEVICE(obj) \
 *       OBJECT_CHECK(MyDevice, obj, TYPE_MY_DEVICE)
 *   </programlisting>
 * </example>
 *
 * # Class Initialization #
 *
 * Before an object is initialized, the class for the object must be
 * initialized.  There is only one class object for all instance objects
 * that is created lazily.
 *
 * Classes are initialized by first initializing any parent classes (if
 * necessary).  After the parent class object has initialized, it will be
 * copied into the current class object and any additional storage in the
 * class object is zero filled.
 *
 * The effect of this is that classes automatically inherit any virtual
 * function pointers that the parent class has already initialized.  All
 * other fields will be zero filled.
 *
 * Once all of the parent classes have been initialized, #TypeInfo::class_init
 * is called to let the class being instantiated provide default initialize for
 * its virtual functions.  Here is how the above example might be modified
 * to introduce an overridden virtual function:
 *
 * <example>
 *   <title>Overriding a virtual function</title>
 *   <programlisting>
 * #include "qdev.h"
 *
 * void my_device_class_init(ObjectClass *klass, void *class_data)
 * {
 *     DeviceClass *dc = DEVICE_CLASS(klass);
 *     dc->reset = my_device_reset;
 * }
 *
 * static const TypeInfo my_device_info = {
 *     .name = TYPE_MY_DEVICE,
 *     .parent = TYPE_DEVICE,
 *     .instance_size = sizeof(MyDevice),
 *     .class_init = my_device_class_init,
 * };
 *   </programlisting>
 * </example>
 *
 * Introducing new virtual methods requires a class to define its own
 * struct and to add a .class_size member to the #TypeInfo.  Each method
 * will also have a wrapper function to call it easily:
 *
 * <example>
 *   <title>Defining an abstract class</title>
 *   <programlisting>
 * #include "qdev.h"
 *
 * typedef struct MyDeviceClass
 * {
 *     DeviceClass parent;
 *
 *     void (*frobnicate) (MyDevice *obj);
 * } MyDeviceClass;
 *
 * static const TypeInfo my_device_info = {
 *     .name = TYPE_MY_DEVICE,
 *     .parent = TYPE_DEVICE,
 *     .instance_size = sizeof(MyDevice),
 *     .abstract = true, // or set a default in my_device_class_init
 *     .class_size = sizeof(MyDeviceClass),
 * };
 *
 * void my_device_frobnicate(MyDevice *obj)
 * {
 *     MyDeviceClass *klass = MY_DEVICE_GET_CLASS(obj);
 *
 *     klass->frobnicate(obj);
 * }
 *   </programlisting>
 * </example>
 *
 * # Interfaces #
 *
 * Interfaces allow a limited form of multiple inheritance.  Instances are
 * similar to normal types except for the fact that are only defined by
 * their classes and never carry any state.  You can dynamically cast an object
 * to one of its #Interface types and vice versa.
 *
 * # Methods #
 *
 * A <emphasis>method</emphasis> is a function within the namespace scope of
 * a class. It usually operates on the object instance by passing it as a
 * strongly-typed first argument.
 * If it does not operate on an object instance, it is dubbed
 * <emphasis>class method</emphasis>.
 *
 * Methods cannot be overloaded. That is, the #ObjectClass and method name
 * uniquely identity the function to be called; the signature does not vary
 * except for trailing varargs.
 *
 * Methods are always <emphasis>virtual</emphasis>. Overriding a method in
 * #TypeInfo.class_init of a subclass leads to any user of the class obtained
 * via OBJECT_GET_CLASS() accessing the overridden function.
 * The original function is not automatically invoked. It is the responsibility
 * of the overriding class to determine whether and when to invoke the method
 * being overridden.
 *
 * To invoke the method being overridden, the preferred solution is to store
 * the original value in the overriding class before overriding the method.
 * This corresponds to |[ {super,base}.method(...) ]| in Java and C#
 * respectively; this frees the overriding class from hardcoding its parent
 * class, which someone might choose to change at some point.
 *
 * <example>
 *   <title>Overriding a virtual method</title>
 *   <programlisting>
 * typedef struct MyState MyState;
 *
 * typedef void (*MyDoSomething)(MyState *obj);
 *
 * typedef struct MyClass {
 *     ObjectClass parent_class;
 *
 *     MyDoSomething do_something;
 * } MyClass;
 *
 * static void my_do_something(MyState *obj)
 * {
 *     // do something
 * }
 *
 * static void my_class_init(ObjectClass *oc, void *data)
 * {
 *     MyClass *mc = MY_CLASS(oc);
 *
 *     mc->do_something = my_do_something;
 * }
 *
 * static const TypeInfo my_type_info = {
 *     .name = TYPE_MY,
 *     .parent = TYPE_OBJECT,
 *     .instance_size = sizeof(MyState),
 *     .class_size = sizeof(MyClass),
 *     .class_init = my_class_init,
 * };
 *
 * typedef struct DerivedClass {
 *     MyClass parent_class;
 *
 *     MyDoSomething parent_do_something;
 * } DerivedClass;
 *
 * static void derived_do_something(MyState *obj)
 * {
 *     DerivedClass *dc = DERIVED_GET_CLASS(obj);
 *
 *     // do something here
 *     dc->parent_do_something(obj);
 *     // do something else here
 * }
 *
 * static void derived_class_init(ObjectClass *oc, void *data)
 * {
 *     MyClass *mc = MY_CLASS(oc);
 *     DerivedClass *dc = DERIVED_CLASS(oc);
 *
 *     dc->parent_do_something = mc->do_something;
 *     mc->do_something = derived_do_something;
 * }
 *
 * static const TypeInfo derived_type_info = {
 *     .name = TYPE_DERIVED,
 *     .parent = TYPE_MY,
 *     .class_size = sizeof(DerivedClass),
 *     .class_init = derived_class_init,
 * };
 *   </programlisting>
 * </example>
 *
 * Alternatively, object_class_by_name() can be used to obtain the class and
 * its non-overridden methods for a specific type. This would correspond to
 * |[ MyClass::method(...) ]| in C++.
 *
 * The first example of such a QOM method was #CPUClass.reset,
 * another example is #DeviceClass.realize.
 */


/**
 * ObjectPropertyAccessor:
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
 * ObjectPropertyResolve:
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
 * ObjectPropertyRelease:
 * @obj: the object that owns the property
 * @name: the name of the property
 * @opaque: the opaque registered with the property
 *
 * Called when a property is removed from a object.
 */
typedef void (ObjectPropertyRelease)(Object *obj,
                                     const char *name,
                                     void *opaque);

typedef struct ObjectProperty
{
    gchar *name;
    gchar *type;
    gchar *description;
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyResolve *resolve;
    ObjectPropertyRelease *release;
    void *opaque;
} ObjectProperty;

/**
 * ObjectUnparent:
 * @obj: the object that is being removed from the composition tree
 *
 * Called when an object is being removed from the QOM composition tree.
 * The function should remove any backlinks from children objects to @obj.
 */
typedef void (ObjectUnparent)(Object *obj);

/**
 * ObjectFree:
 * @obj: the object being freed
 *
 * Called when an object's last reference is removed.
 */
typedef void (ObjectFree)(void *obj);

#define OBJECT_CLASS_CAST_CACHE 4

/**
 * ObjectClass:
 *
 * The base for all classes.  The only thing that #ObjectClass contains is an
 * integer type handle.
 */
struct ObjectClass
{
    /*< private >*/
    Type type;
    GSList *interfaces;

    const char *object_cast_cache[OBJECT_CLASS_CAST_CACHE];
    const char *class_cast_cache[OBJECT_CLASS_CAST_CACHE];

    ObjectUnparent *unparent;

    GHashTable *properties;
};

/**
 * Object:
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
    /*< private >*/
    ObjectClass *class;
    ObjectFree *free;
    GHashTable *properties;
    uint32_t ref;
    Object *parent;
};

/**
 * TypeInfo:
 * @name: The name of the type.
 * @parent: The name of the parent type.
 * @instance_size: The size of the object (derivative of #Object).  If
 *   @instance_size is 0, then the size of the object will be the size of the
 *   parent object.
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
 *   memcpy from the parent class to the descendents.
 * @class_finalize: This function is called during class destruction and is
 *   meant to release and dynamic parameters allocated by @class_init.
 * @class_data: Data to pass to the @class_init, @class_base_init and
 *   @class_finalize functions.  This can be useful when building dynamic
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
    void (*instance_init)(Object *obj);
    void (*instance_post_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;
    size_t class_size;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_base_init)(ObjectClass *klass, void *data);
    void (*class_finalize)(ObjectClass *klass, void *data);
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
 * InterfaceInfo:
 * @type: The name of the interface.
 *
 * The information associated with an interface.
 */
struct InterfaceInfo {
    const char *type;
};

/**
 * InterfaceClass:
 * @parent_class: the base class
 *
 * The class for all interfaces.  Subclasses of this class should only add
 * virtual methods.
 */
struct InterfaceClass
{
    ObjectClass parent_class;
    /*< private >*/
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
 * object_new_with_type:
 * @type: The type of the object to instantiate.
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 *
 * Returns: The newly allocated and instantiated object.
 */
Object *object_new_with_type(Type type);

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
 * <example>
 *   <title>Creating an object with properties</title>
 *   <programlisting>
 *   Error *err = NULL;
 *   Object *obj;
 *
 *   obj = object_new_with_props(TYPE_MEMORY_BACKEND_FILE,
 *                               object_get_objects_root(),
 *                               "hostmem0",
 *                               &err,
 *                               "share", "yes",
 *                               "mem-path", "/dev/shm/somefile",
 *                               "prealloc", "yes",
 *                               "size", "1048576",
 *                               NULL);
 *
 *   if (!obj) {
 *     g_printerr("Cannot create memory backend: %s\n",
 *                error_get_pretty(err));
 *   }
 *   </programlisting>
 * </example>
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
 * <example>
 *   <title>Update an object's properties</title>
 *   <programlisting>
 *   Error *err = NULL;
 *   Object *obj = ...get / create object...;
 *
 *   obj = object_set_props(obj,
 *                          &err,
 *                          "share", "yes",
 *                          "mem-path", "/dev/shm/somefile",
 *                          "prealloc", "yes",
 *                          "size", "1048576",
 *                          NULL);
 *
 *   if (!obj) {
 *     g_printerr("Cannot set properties: %s\n",
 *                error_get_pretty(err));
 *   }
 *   </programlisting>
 * </example>
 *
 * The returned object will have one stable reference maintained
 * for as long as it is present in the object hierarchy.
 *
 * Returns: -1 on error, 0 on success
 */
int object_set_props(Object *obj,
                     Error **errp,
                     ...) QEMU_SENTINEL;

/**
 * object_set_propv:
 * @obj: the object instance to set properties on
 * @errp: pointer to error object
 * @vargs: list of property names and values
 *
 * See object_set_props() for documentation.
 *
 * Returns: -1 on error, 0 on success
 */
int object_set_propv(Object *obj,
                     Error **errp,
                     va_list vargs);

/**
 * object_initialize_with_type:
 * @data: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @data for the object.
 * @type: The type of the object to instantiate.
 *
 * This function will initialize an object.  The memory for the object should
 * have already been allocated.  The returned object has a reference count of 1,
 * and will be finalized when the last reference is dropped.
 */
void object_initialize_with_type(void *data, size_t size, Type type);

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
const char *object_get_typename(Object *obj);

/**
 * type_register_static:
 * @info: The #TypeInfo of the new type.
 *
 * @info and all of the strings it points to should exist for the life time
 * that the type is registered.
 *
 * Returns: 0 on failure, the new #Type on success.
 */
Type type_register_static(const TypeInfo *info);

/**
 * type_register:
 * @info: The #TypeInfo of the new type
 *
 * Unlike type_register_static(), this call does not require @info or its
 * string members to continue to exist after the call returns.
 *
 * Returns: 0 on failure, the new #Type on success.
 */
Type type_register(const TypeInfo *info);

/**
 * object_class_dynamic_cast_assert:
 * @klass: The #ObjectClass to attempt to cast.
 * @typename: The QOM typename of the class to cast to.
 *
 * See object_class_dynamic_cast() for a description of the parameters
 * of this function.  The only difference in behavior is that this function
 * asserts instead of returning #NULL on failure if QOM cast debugging is
 * enabled.  This function is not meant to be called directly, but only through
 * the wrapper macros OBJECT_CLASS_CHECK and INTERFACE_CHECK.
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
 * object_ref:
 * @obj: the object
 *
 * Increase the reference count of a object.  A object cannot be freed as long
 * as its reference count is greater than zero.
 */
void object_ref(Object *obj);

/**
 * qdef_unref:
 * @obj: the object
 *
 * Decrease the reference count of a object.  A object cannot be freed as long
 * as its reference count is greater than zero.
 */
void object_unref(Object *obj);

/**
 * object_property_add:
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
 * @errp: returns an error if this function fails
 *
 * Returns: The #ObjectProperty; this can be used to set the @resolve
 * callback for child and link properties.
 */
ObjectProperty *object_property_add(Object *obj, const char *name,
                                    const char *type,
                                    ObjectPropertyAccessor *get,
                                    ObjectPropertyAccessor *set,
                                    ObjectPropertyRelease *release,
                                    void *opaque, Error **errp);

void object_property_del(Object *obj, const char *name, Error **errp);

ObjectProperty *object_class_property_add(ObjectClass *klass, const char *name,
                                          const char *type,
                                          ObjectPropertyAccessor *get,
                                          ObjectPropertyAccessor *set,
                                          ObjectPropertyRelease *release,
                                          void *opaque, Error **errp);

/**
 * object_property_find:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Look up a property for an object and return its #ObjectProperty if found.
 */
ObjectProperty *object_property_find(Object *obj, const char *name,
                                     Error **errp);
ObjectProperty *object_class_property_find(ObjectClass *klass, const char *name,
                                           Error **errp);

typedef struct ObjectPropertyIterator {
    ObjectClass *nextclass;
    GHashTableIter iter;
} ObjectPropertyIterator;

/**
 * object_property_iter_init:
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
 * <example>
 *   <title>Using object property iterators</title>
 *   <programlisting>
 *   ObjectProperty *prop;
 *   ObjectPropertyIterator iter;
 *
 *   object_property_iter_init(&iter, obj);
 *   while ((prop = object_property_iter_next(&iter))) {
 *     ... do something with prop ...
 *   }
 *   </programlisting>
 * </example>
 */
void object_property_iter_init(ObjectPropertyIterator *iter,
                               Object *obj);

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
 * @v: the visitor that will receive the property value.  This should be an
 *   Output visitor and the data will be written with @name as the name.
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Reads a property from a object.
 */
void object_property_get(Object *obj, Visitor *v, const char *name,
                         Error **errp);

/**
 * object_property_set_str:
 * @value: the value to be written to the property
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Writes a string value to a property.
 */
void object_property_set_str(Object *obj, const char *value,
                             const char *name, Error **errp);

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
 * @value: the value to be written to the property
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Writes an object's canonical path to a property.
 */
void object_property_set_link(Object *obj, Object *value,
                              const char *name, Error **errp);

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
 * @value: the value to be written to the property
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Writes a bool value to a property.
 */
void object_property_set_bool(Object *obj, bool value,
                              const char *name, Error **errp);

/**
 * object_property_get_bool:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to a boolean, or NULL if
 * an error occurs (including when the property value is not a bool).
 */
bool object_property_get_bool(Object *obj, const char *name,
                              Error **errp);

/**
 * object_property_set_int:
 * @value: the value to be written to the property
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Writes an integer value to a property.
 */
void object_property_set_int(Object *obj, int64_t value,
                             const char *name, Error **errp);

/**
 * object_property_get_int:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an integer, or negative if
 * an error occurs (including when the property value is not an integer).
 */
int64_t object_property_get_int(Object *obj, const char *name,
                                Error **errp);

/**
 * object_property_get_enum:
 * @obj: the object
 * @name: the name of the property
 * @typename: the name of the enum data type
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an integer, or
 * undefined if an error occurs (including when the property value is not
 * an enum).
 */
int object_property_get_enum(Object *obj, const char *name,
                             const char *typename, Error **errp);

/**
 * object_property_get_uint16List:
 * @obj: the object
 * @name: the name of the property
 * @list: the returned int list
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to integers, or
 * undefined if an error occurs (including when the property value is not
 * an list of integers).
 */
void object_property_get_uint16List(Object *obj, const char *name,
                                    uint16List **list, Error **errp);

/**
 * object_property_set:
 * @obj: the object
 * @v: the visitor that will be used to write the property value.  This should
 *   be an Input visitor and the data will be first read with @name as the
 *   name and then written as the property value.
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Writes a property to a object.
 */
void object_property_set(Object *obj, Visitor *v, const char *name,
                         Error **errp);

/**
 * object_property_parse:
 * @obj: the object
 * @string: the string that will be used to parse the property value.
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Parses a string and writes the result into a property of an object.
 */
void object_property_parse(Object *obj, const char *string,
                           const char *name, Error **errp);

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
 * object_get_canonical_path_component:
 *
 * Returns: The final component in the object's canonical path.  The canonical
 * path is the path within the composition tree starting from the root.
 */
gchar *object_get_canonical_path_component(Object *obj);

/**
 * object_get_canonical_path:
 *
 * Returns: The canonical path for a object.  This is the path within the
 * composition tree starting from the root.
 */
gchar *object_get_canonical_path(Object *obj);

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
 * object_resolve_path_component:
 * @parent: the object in which to resolve the path
 * @part: the component to resolve.
 *
 * This is similar to object_resolve_path with an absolute path, but it
 * only resolves one element (@part) and takes the others from @parent.
 *
 * Returns: The resolved object or NULL on path lookup failure.
 */
Object *object_resolve_path_component(Object *parent, const gchar *part);

/**
 * object_property_add_child:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @child: the child object
 * @errp: if an error occurs, a pointer to an area to store the area
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
 */
void object_property_add_child(Object *obj, const char *name,
                               Object *child, Error **errp);

typedef enum {
    /* Unref the link pointer when the property is deleted */
    OBJ_PROP_LINK_UNREF_ON_RELEASE = 0x1,
} ObjectPropertyLinkFlags;

/**
 * object_property_allow_set_link:
 *
 * The default implementation of the object_property_add_link() check()
 * callback function.  It allows the link property to be set and never returns
 * an error.
 */
void object_property_allow_set_link(Object *, const char *,
                                    Object *, Error **);

/**
 * object_property_add_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @type: the qobj type of the link
 * @child: a pointer to where the link object reference is stored
 * @check: callback to veto setting or NULL if the property is read-only
 * @flags: additional options for the link
 * @errp: if an error occurs, a pointer to an area to store the area
 *
 * Links establish relationships between objects.  Links are unidirectional
 * although two links can be combined to form a bidirectional relationship
 * between objects.
 *
 * Links form the graph in the object model.
 *
 * The <code>@check()</code> callback is invoked when
 * object_property_set_link() is called and can raise an error to prevent the
 * link being set.  If <code>@check</code> is NULL, the property is read-only
 * and cannot be set.
 *
 * Ownership of the pointer that @child points to is transferred to the
 * link property.  The reference count for <code>*@child</code> is
 * managed by the property from after the function returns till the
 * property is deleted with object_property_del().  If the
 * <code>@flags</code> <code>OBJ_PROP_LINK_UNREF_ON_RELEASE</code> bit is set,
 * the reference count is decremented when the property is deleted.
 */
void object_property_add_link(Object *obj, const char *name,
                              const char *type, Object **child,
                              void (*check)(Object *obj, const char *name,
                                            Object *val, Error **errp),
                              ObjectPropertyLinkFlags flags,
                              Error **errp);

/**
 * object_property_add_str:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.  This function must
 *   return a string to be freed by g_free().
 * @set: the setter or NULL if the property is read-only
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add a string property using getters/setters.  This function will add a
 * property of type 'string'.
 */
void object_property_add_str(Object *obj, const char *name,
                             char *(*get)(Object *, Error **),
                             void (*set)(Object *, const char *, Error **),
                             Error **errp);

void object_class_property_add_str(ObjectClass *klass, const char *name,
                                   char *(*get)(Object *, Error **),
                                   void (*set)(Object *, const char *,
                                               Error **),
                                   Error **errp);

/**
 * object_property_add_bool:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.
 * @set: the setter or NULL if the property is read-only
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add a bool property using getters/setters.  This function will add a
 * property of type 'bool'.
 */
void object_property_add_bool(Object *obj, const char *name,
                              bool (*get)(Object *, Error **),
                              void (*set)(Object *, bool, Error **),
                              Error **errp);

void object_class_property_add_bool(ObjectClass *klass, const char *name,
                                    bool (*get)(Object *, Error **),
                                    void (*set)(Object *, bool, Error **),
                                    Error **errp);

/**
 * object_property_add_enum:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @typename: the name of the enum data type
 * @get: the getter or %NULL if the property is write-only.
 * @set: the setter or %NULL if the property is read-only
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an enum property using getters/setters.  This function will add a
 * property of type '@typename'.
 */
void object_property_add_enum(Object *obj, const char *name,
                              const char *typename,
                              const char * const *strings,
                              int (*get)(Object *, Error **),
                              void (*set)(Object *, int, Error **),
                              Error **errp);

void object_class_property_add_enum(ObjectClass *klass, const char *name,
                                    const char *typename,
                                    const char * const *strings,
                                    int (*get)(Object *, Error **),
                                    void (*set)(Object *, int, Error **),
                                    Error **errp);

/**
 * object_property_add_tm:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add a read-only struct tm valued property using a getter function.
 * This function will add a property of type 'struct tm'.
 */
void object_property_add_tm(Object *obj, const char *name,
                            void (*get)(Object *, struct tm *, Error **),
                            Error **errp);

void object_class_property_add_tm(ObjectClass *klass, const char *name,
                                  void (*get)(Object *, struct tm *, Error **),
                                  Error **errp);

/**
 * object_property_add_uint8_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint8'.
 */
void object_property_add_uint8_ptr(Object *obj, const char *name,
                                   const uint8_t *v, Error **errp);
void object_class_property_add_uint8_ptr(ObjectClass *klass, const char *name,
                                         const uint8_t *v, Error **errp);

/**
 * object_property_add_uint16_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint16'.
 */
void object_property_add_uint16_ptr(Object *obj, const char *name,
                                    const uint16_t *v, Error **errp);
void object_class_property_add_uint16_ptr(ObjectClass *klass, const char *name,
                                          const uint16_t *v, Error **errp);

/**
 * object_property_add_uint32_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint32'.
 */
void object_property_add_uint32_ptr(Object *obj, const char *name,
                                    const uint32_t *v, Error **errp);
void object_class_property_add_uint32_ptr(ObjectClass *klass, const char *name,
                                          const uint32_t *v, Error **errp);

/**
 * object_property_add_uint64_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint64'.
 */
void object_property_add_uint64_ptr(Object *obj, const char *name,
                                    const uint64_t *v, Error **Errp);
void object_class_property_add_uint64_ptr(ObjectClass *klass, const char *name,
                                          const uint64_t *v, Error **Errp);

/**
 * object_property_add_alias:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @target_obj: the object to forward property access to
 * @target_name: the name of the property on the forwarded object
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an alias for a property on an object.  This function will add a property
 * of the same type as the forwarded property.
 *
 * The caller must ensure that <code>@target_obj</code> stays alive as long as
 * this property exists.  In the case of a child object or an alias on the same
 * object this will be the case.  For aliases to other objects the caller is
 * responsible for taking a reference.
 */
void object_property_add_alias(Object *obj, const char *name,
                               Object *target_obj, const char *target_name,
                               Error **errp);

/**
 * object_property_add_const_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @target: the object to be referred by the link
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Add an unmodifiable link for a property on an object.  This function will
 * add a property of type link<TYPE> where TYPE is the type of @target.
 *
 * The caller must ensure that @target stays alive as long as
 * this property exists.  In the case @target is a child of @obj,
 * this will be the case.  Otherwise, the caller is responsible for
 * taking a reference.
 */
void object_property_add_const_link(Object *obj, const char *name,
                                    Object *target, Error **errp);

/**
 * object_property_set_description:
 * @obj: the object owning the property
 * @name: the name of the property
 * @description: the description of the property on the object
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Set an object property's description.
 *
 */
void object_property_set_description(Object *obj, const char *name,
                                     const char *description, Error **errp);
void object_class_property_set_description(ObjectClass *klass, const char *name,
                                           const char *description,
                                           Error **errp);

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


#endif
