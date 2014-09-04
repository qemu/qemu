#ifndef OBJECT_INTERFACES_H
#define OBJECT_INTERFACES_H

#include "qom/object.h"

#define TYPE_USER_CREATABLE "user-creatable"

#define USER_CREATABLE_CLASS(klass) \
     OBJECT_CLASS_CHECK(UserCreatableClass, (klass), \
                        TYPE_USER_CREATABLE)
#define USER_CREATABLE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(UserCreatableClass, (obj), \
                      TYPE_USER_CREATABLE)
#define USER_CREATABLE(obj) \
     INTERFACE_CHECK(UserCreatable, (obj), \
                     TYPE_USER_CREATABLE)


typedef struct UserCreatable {
    /* <private> */
    Object Parent;
} UserCreatable;

/**
 * UserCreatableClass:
 * @parent_class: the base class
 * @complete: callback to be called after @obj's properties are set.
 *
 * Interface is designed to work with -object/object-add/object_add
 * commands.
 * Interface is mandatory for objects that are designed to be user
 * creatable (i.e. -object/object-add/object_add, will accept only
 * objects that inherit this interface).
 *
 * Interface also provides an optional ability to do the second
 * stage * initialization of the object after its properties were
 * set.
 *
 * For objects created without using -object/object-add/object_add,
 * @user_creatable_complete() wrapper should be called manually if
 * object's type implements USER_CREATABLE interface and needs
 * complete() callback to be called.
 */
typedef struct UserCreatableClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*complete)(UserCreatable *uc, Error **errp);
} UserCreatableClass;

/**
 * user_creatable_complete:
 * @obj: the object whose complete() method is called if defined
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Wrapper to call complete() method if one of types it's inherited
 * from implements USER_CREATABLE interface, otherwise the call does
 * nothing.
 */
void user_creatable_complete(Object *obj, Error **errp);
#endif
