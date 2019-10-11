#ifndef OBJECT_INTERFACES_H
#define OBJECT_INTERFACES_H

#include "qom/object.h"
#include "qapi/visitor.h"

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

typedef struct UserCreatable UserCreatable;

/**
 * UserCreatableClass:
 * @parent_class: the base class
 * @complete: callback to be called after @obj's properties are set.
 * @can_be_deleted: callback to be called before an object is removed
 * to check if @obj can be removed safely.
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
    bool (*can_be_deleted)(UserCreatable *uc);
} UserCreatableClass;

/**
 * user_creatable_complete:
 * @uc: the user-creatable object whose complete() method is called if defined
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Wrapper to call complete() method if one of types it's inherited
 * from implements USER_CREATABLE interface, otherwise the call does
 * nothing.
 */
void user_creatable_complete(UserCreatable *uc, Error **errp);

/**
 * user_creatable_can_be_deleted:
 * @uc: the object whose can_be_deleted() method is called if implemented
 *
 * Wrapper to call can_be_deleted() method if one of types it's inherited
 * from implements USER_CREATABLE interface.
 */
bool user_creatable_can_be_deleted(UserCreatable *uc);

/**
 * user_creatable_add_type:
 * @type: the object type name
 * @id: the unique ID for the object
 * @qdict: the object properties
 * @v: the visitor
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object @type, placing
 * it in the object composition tree with name @id, initializing
 * it with properties from @qdict
 *
 * Returns: the newly created object or NULL on error
 */
Object *user_creatable_add_type(const char *type, const char *id,
                                const QDict *qdict,
                                Visitor *v, Error **errp);

/**
 * user_creatable_add_opts:
 * @opts: the object definition
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object whose type
 * is defined in @opts by the 'qom-type' option, placing it
 * in the object composition tree with name provided by the
 * 'id' field. The remaining options in @opts are used to
 * initialize the object properties.
 *
 * Returns: the newly created object or NULL on error
 */
Object *user_creatable_add_opts(QemuOpts *opts, Error **errp);


/**
 * user_creatable_add_opts_predicate:
 * @type: the QOM type to be added
 *
 * A callback function to determine whether an object
 * of type @type should be created. Instances of this
 * callback should be passed to user_creatable_add_opts_foreach
 */
typedef bool (*user_creatable_add_opts_predicate)(const char *type);

/**
 * user_creatable_add_opts_foreach:
 * @opaque: a user_creatable_add_opts_predicate callback or NULL
 * @opts: options to create
 * @errp: unused
 *
 * An iterator callback to be used in conjunction with
 * the qemu_opts_foreach() method for creating a list of
 * objects from a set of QemuOpts
 *
 * The @opaque parameter can be passed a user_creatable_add_opts_predicate
 * callback to filter which types of object are created during iteration.
 * When it fails, report the error.
 *
 * Returns: 0 on success, -1 when an error was reported.
 */
int user_creatable_add_opts_foreach(void *opaque,
                                    QemuOpts *opts, Error **errp);

/**
 * user_creatable_print_help:
 * @type: the QOM type to be added
 * @opts: options to create
 *
 * Prints help if requested in @opts.
 *
 * Returns: true if @opts contained a help option and help was printed, false
 * if no help option was found.
 */
bool user_creatable_print_help(const char *type, QemuOpts *opts);

/**
 * user_creatable_del:
 * @id: the unique ID for the object
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Delete an instance of the user creatable object identified
 * by @id.
 */
void user_creatable_del(const char *id, Error **errp);

/**
 * user_creatable_cleanup:
 *
 * Delete all user-creatable objects and the user-creatable
 * objects container.
 */
void user_creatable_cleanup(void);

#endif
