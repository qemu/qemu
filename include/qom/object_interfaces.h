#ifndef OBJECT_INTERFACES_H
#define OBJECT_INTERFACES_H

#include "qom/object.h"
#include "qapi/qapi-types-qom.h"
#include "qapi/visitor.h"

#define TYPE_USER_CREATABLE "user-creatable"

typedef struct UserCreatableClass UserCreatableClass;
DECLARE_CLASS_CHECKERS(UserCreatableClass, USER_CREATABLE,
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
struct UserCreatableClass {
    /* <private> */
    InterfaceClass parent_class;

    /* <public> */
    void (*complete)(UserCreatable *uc, Error **errp);
    bool (*can_be_deleted)(UserCreatable *uc);
};

/**
 * user_creatable_complete:
 * @uc: the user-creatable object whose complete() method is called if defined
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Wrapper to call complete() method if one of types it's inherited
 * from implements USER_CREATABLE interface, otherwise the call does
 * nothing.
 *
 * Returns: %true on success, %false on failure.
 */
bool user_creatable_complete(UserCreatable *uc, Error **errp);

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
 * user_creatable_add_qapi:
 * @options: the object definition
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object according to the
 * options passed in @opts as described in the QAPI schema documentation.
 */
void user_creatable_add_qapi(ObjectOptions *options, Error **errp);

/**
 * user_creatable_parse_str:
 * @str: the object definition string as passed on the command line
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Parses the option for the user creatable object with a keyval parser and
 * implicit key 'qom-type', converting the result to ObjectOptions.
 *
 * If a help option is given, print help instead.
 *
 * Returns: ObjectOptions on success, NULL when an error occurred (*errp is set
 * then) or help was printed (*errp is not set).
 */
ObjectOptions *user_creatable_parse_str(const char *str, Error **errp);

/**
 * user_creatable_add_from_str:
 * @str: the object definition string as passed on the command line
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Create an instance of the user creatable object by parsing @str
 * with a keyval parser and implicit key 'qom-type', converting the
 * result to ObjectOptions and calling into qmp_object_add().
 *
 * If a help option is given, print help instead.
 *
 * Returns: true when an object was successfully created, false when an error
 * occurred (*errp is set then) or help was printed (*errp is not set).
 */
bool user_creatable_add_from_str(const char *str, Error **errp);

/**
 * user_creatable_process_cmdline:
 * @cmdline: the object definition string as passed on the command line
 *
 * Create an instance of the user creatable object by parsing @cmdline
 * with a keyval parser and implicit key 'qom-type', converting the
 * result to ObjectOptions and calling into qmp_object_add().
 *
 * If a help option is given, print help instead and exit.
 *
 * This function is only meant to be called during command line parsing.
 * It exits the process on failure or after printing help.
 */
void user_creatable_process_cmdline(const char *cmdline);

/**
 * user_creatable_print_help:
 * @type: the QOM type to be added
 * @opts: options to create
 *
 * Prints help if requested in @type or @opts. Note that if @type is neither
 * "help"/"?" nor a valid user creatable type, no help will be printed
 * regardless of @opts.
 *
 * Returns: true if a help option was found and help was printed, false
 * otherwise.
 */
bool user_creatable_print_help(const char *type, QemuOpts *opts);

/**
 * user_creatable_del:
 * @id: the unique ID for the object
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Delete an instance of the user creatable object identified
 * by @id.
 *
 * Returns: %true on success, %false on failure.
 */
bool user_creatable_del(const char *id, Error **errp);

/**
 * user_creatable_cleanup:
 *
 * Delete all user-creatable objects and the user-creatable
 * objects container.
 */
void user_creatable_cleanup(void);

#endif
