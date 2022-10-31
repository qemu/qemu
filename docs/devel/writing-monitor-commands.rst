How to write monitor commands
=============================

This document is a step-by-step guide on how to write new QMP commands using
the QAPI framework and HMP commands.

This document doesn't discuss QMP protocol level details, nor does it dive
into the QAPI framework implementation.

For an in-depth introduction to the QAPI framework, please refer to
docs/devel/qapi-code-gen.txt. For documentation about the QMP protocol,
start with docs/interop/qmp-intro.txt.

New commands may be implemented in QMP only.  New HMP commands should be
implemented on top of QMP.  The typical HMP command wraps around an
equivalent QMP command, but HMP convenience commands built from QMP
building blocks are also fine.  The long term goal is to make all
existing HMP commands conform to this, to fully isolate HMP from the
internals of QEMU. Refer to the `Writing a debugging aid returning
unstructured text`_ section for further guidance on commands that
would have traditionally been HMP only.

Overview
--------

Generally speaking, the following steps should be taken in order to write a
new QMP command.

1. Define the command and any types it needs in the appropriate QAPI
   schema module.

2. Write the QMP command itself, which is a regular C function. Preferably,
   the command should be exported by some QEMU subsystem. But it can also be
   added to the monitor/qmp-cmds.c file

3. At this point the command can be tested under the QMP protocol

4. Write the HMP command equivalent. This is not required and should only be
   done if it does make sense to have the functionality in HMP. The HMP command
   is implemented in terms of the QMP command

The following sections will demonstrate each of the steps above. We will start
very simple and get more complex as we progress.


Testing
-------

For all the examples in the next sections, the test setup is the same and is
shown here.

First, QEMU should be started like this::

 # qemu-system-TARGET [...] \
     -chardev socket,id=qmp,port=4444,host=localhost,server=on \
     -mon chardev=qmp,mode=control,pretty=on

Then, in a different terminal::

 $ telnet localhost 4444
 Trying 127.0.0.1...
 Connected to localhost.
 Escape character is '^]'.
 {
     "QMP": {
         "version": {
             "qemu": {
                 "micro": 50,
                 "minor": 15,
                 "major": 0
             },
             "package": ""
         },
         "capabilities": [
         ]
     }
 }

The above output is the QMP server saying you're connected. The server is
actually in capabilities negotiation mode. To enter in command mode type::

 { "execute": "qmp_capabilities" }

Then the server should respond::

 {
     "return": {
     }
 }

Which is QMP's way of saying "the latest command executed OK and didn't return
any data". Now you're ready to enter the QMP example commands as explained in
the following sections.


Writing a simple command: hello-world
-------------------------------------

That's the most simple QMP command that can be written. Usually, this kind of
command carries some meaningful action in QEMU but here it will just print
"Hello, world" to the standard output.

Our command will be called "hello-world". It takes no arguments, nor does it
return any data.

The first step is defining the command in the appropriate QAPI schema
module.  We pick module qapi/misc.json, and add the following line at
the bottom::

 { 'command': 'hello-world' }

The "command" keyword defines a new QMP command. It's an JSON object. All
schema entries are JSON objects. The line above will instruct the QAPI to
generate any prototypes and the necessary code to marshal and unmarshal
protocol data.

The next step is to write the "hello-world" implementation. As explained
earlier, it's preferable for commands to live in QEMU subsystems. But
"hello-world" doesn't pertain to any, so we put its implementation in
monitor/qmp-cmds.c::

 void qmp_hello_world(Error **errp)
 {
     printf("Hello, world!\n");
 }

There are a few things to be noticed:

1. QMP command implementation functions must be prefixed with "qmp\_"
2. qmp_hello_world() returns void, this is in accordance with the fact that the
   command doesn't return any data
3. It takes an "Error \*\*" argument. This is required. Later we will see how to
   return errors and take additional arguments. The Error argument should not
   be touched if the command doesn't return errors
4. We won't add the function's prototype. That's automatically done by the QAPI
5. Printing to the terminal is discouraged for QMP commands, we do it here
   because it's the easiest way to demonstrate a QMP command

You're done. Now build qemu, run it as suggested in the "Testing" section,
and then type the following QMP command::

 { "execute": "hello-world" }

Then check the terminal running qemu and look for the "Hello, world" string. If
you don't see it then something went wrong.


Arguments
~~~~~~~~~

Let's add an argument called "message" to our "hello-world" command. The new
argument will contain the string to be printed to stdout. It's an optional
argument, if it's not present we print our default "Hello, World" string.

The first change we have to do is to modify the command specification in the
schema file to the following::

 { 'command': 'hello-world', 'data': { '*message': 'str' } }

Notice the new 'data' member in the schema. It's an JSON object whose each
element is an argument to the command in question. Also notice the asterisk,
it's used to mark the argument optional (that means that you shouldn't use it
for mandatory arguments). Finally, 'str' is the argument's type, which
stands for "string". The QAPI also supports integers, booleans, enumerations
and user defined types.

Now, let's update our C implementation in monitor/qmp-cmds.c::

 void qmp_hello_world(bool has_message, const char *message, Error **errp)
 {
     if (has_message) {
         printf("%s\n", message);
     } else {
         printf("Hello, world\n");
     }
 }

There are two important details to be noticed:

1. All optional arguments are accompanied by a 'has\_' boolean, which is set
   if the optional argument is present or false otherwise
2. The C implementation signature must follow the schema's argument ordering,
   which is defined by the "data" member

Time to test our new version of the "hello-world" command. Build qemu, run it as
described in the "Testing" section and then send two commands::

 { "execute": "hello-world" }
 {
     "return": {
     }
 }

 { "execute": "hello-world", "arguments": { "message": "We love qemu" } }
 {
     "return": {
     }
 }

You should see "Hello, world" and "We love qemu" in the terminal running qemu,
if you don't see these strings, then something went wrong.


Errors
~~~~~~

QMP commands should use the error interface exported by the error.h header
file. Basically, most errors are set by calling the error_setg() function.

Let's say we don't accept the string "message" to contain the word "love". If
it does contain it, we want the "hello-world" command to return an error::

 void qmp_hello_world(bool has_message, const char *message, Error **errp)
 {
     if (has_message) {
         if (strstr(message, "love")) {
             error_setg(errp, "the word 'love' is not allowed");
             return;
         }
         printf("%s\n", message);
     } else {
         printf("Hello, world\n");
     }
 }

The first argument to the error_setg() function is the Error pointer
to pointer, which is passed to all QMP functions. The next argument is a human
description of the error, this is a free-form printf-like string.

Let's test the example above. Build qemu, run it as defined in the "Testing"
section, and then issue the following command::

 { "execute": "hello-world", "arguments": { "message": "all you need is love" } }

The QMP server's response should be::

 {
     "error": {
         "class": "GenericError",
         "desc": "the word 'love' is not allowed"
     }
 }

Note that error_setg() produces a "GenericError" class.  In general,
all QMP errors should have that error class.  There are two exceptions
to this rule:

 1. To support a management application's need to recognize a specific
    error for special handling

 2. Backward compatibility

If the failure you want to report falls into one of the two cases above,
use error_set() with a second argument of an ErrorClass value.


Command Documentation
~~~~~~~~~~~~~~~~~~~~~

There's only one step missing to make "hello-world"'s implementation complete,
and that's its documentation in the schema file.

There are many examples of such documentation in the schema file already, but
here goes "hello-world"'s new entry for qapi/misc.json::

 ##
 # @hello-world:
 #
 # Print a client provided string to the standard output stream.
 #
 # @message: string to be printed
 #
 # Returns: Nothing on success.
 #
 # Notes: if @message is not provided, the "Hello, world" string will
 #        be printed instead
 #
 # Since: <next qemu stable release, eg. 1.0>
 ##
 { 'command': 'hello-world', 'data': { '*message': 'str' } }

Please, note that the "Returns" clause is optional if a command doesn't return
any data nor any errors.


Implementing the HMP command
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that the QMP command is in place, we can also make it available in the human
monitor (HMP).

With the introduction of the QAPI, HMP commands make QMP calls. Most of the
time HMP commands are simple wrappers. All HMP commands implementation exist in
the monitor/hmp-cmds.c file.

Here's the implementation of the "hello-world" HMP command::

 void hmp_hello_world(Monitor *mon, const QDict *qdict)
 {
     const char *message = qdict_get_try_str(qdict, "message");
     Error *err = NULL;

     qmp_hello_world(!!message, message, &err);
     if (hmp_handle_error(mon, err)) {
         return;
     }
 }

Also, you have to add the function's prototype to the hmp.h file.

There are three important points to be noticed:

1. The "mon" and "qdict" arguments are mandatory for all HMP functions. The
   former is the monitor object. The latter is how the monitor passes
   arguments entered by the user to the command implementation
2. hmp_hello_world() performs error checking. In this example we just call
   hmp_handle_error() which prints a message to the user, but we could do
   more, like taking different actions depending on the error
   qmp_hello_world() returns
3. The "err" variable must be initialized to NULL before performing the
   QMP call

There's one last step to actually make the command available to monitor users,
we should add it to the hmp-commands.hx file::

    {
        .name       = "hello-world",
        .args_type  = "message:s?",
        .params     = "hello-world [message]",
        .help       = "Print message to the standard output",
        .cmd        = hmp_hello_world,
    },

 SRST
 ``hello_world`` *message*
   Print message to the standard output
 ERST

To test this you have to open a user monitor and issue the "hello-world"
command. It might be instructive to check the command's documentation with
HMP's "help" command.

Please, check the "-monitor" command-line option to know how to open a user
monitor.


Writing more complex commands
-----------------------------

A QMP command is capable of returning any data the QAPI supports like integers,
strings, booleans, enumerations and user defined types.

In this section we will focus on user defined types. Please, check the QAPI
documentation for information about the other types.


Modelling data in QAPI
~~~~~~~~~~~~~~~~~~~~~~

For a QMP command that to be considered stable and supported long term,
there is a requirement returned data should be explicitly modelled
using fine-grained QAPI types. As a general guide, a caller of the QMP
command should never need to parse individual returned data fields. If
a field appears to need parsing, then it should be split into separate
fields corresponding to each distinct data item. This should be the
common case for any new QMP command that is intended to be used by
machines, as opposed to exclusively human operators.

Some QMP commands, however, are only intended as ad hoc debugging aids
for human operators. While they may return large amounts of formatted
data, it is not expected that machines will need to parse the result.
The overhead of defining a fine grained QAPI type for the data may not
be justified by the potential benefit. In such cases, it is permitted
to have a command return a simple string that contains formatted data,
however, it is mandatory for the command to use the 'x-' name prefix.
This indicates that the command is not guaranteed to be long term
stable / liable to change in future and is not following QAPI design
best practices. An example where this approach is taken is the QMP
command "x-query-registers". This returns a formatted dump of the
architecture specific CPU state. The way the data is formatted varies
across QEMU targets, is liable to change over time, and is only
intended to be consumed as an opaque string by machines. Refer to the
`Writing a debugging aid returning unstructured text`_ section for
an illustration.

User Defined Types
~~~~~~~~~~~~~~~~~~

FIXME This example needs to be redone after commit 6d32717

For this example we will write the query-alarm-clock command, which returns
information about QEMU's timer alarm. For more information about it, please
check the "-clock" command-line option.

We want to return two pieces of information. The first one is the alarm clock's
name. The second one is when the next alarm will fire. The former information is
returned as a string, the latter is an integer in nanoseconds (which is not
very useful in practice, as the timer has probably already fired when the
information reaches the client).

The best way to return that data is to create a new QAPI type, as shown below::

 ##
 # @QemuAlarmClock
 #
 # QEMU alarm clock information.
 #
 # @clock-name: The alarm clock method's name.
 #
 # @next-deadline: The time (in nanoseconds) the next alarm will fire.
 #
 # Since: 1.0
 ##
 { 'type': 'QemuAlarmClock',
   'data': { 'clock-name': 'str', '*next-deadline': 'int' } }

The "type" keyword defines a new QAPI type. Its "data" member contains the
type's members. In this example our members are the "clock-name" and the
"next-deadline" one, which is optional.

Now let's define the query-alarm-clock command::

 ##
 # @query-alarm-clock
 #
 # Return information about QEMU's alarm clock.
 #
 # Returns a @QemuAlarmClock instance describing the alarm clock method
 # being currently used by QEMU (this is usually set by the '-clock'
 # command-line option).
 #
 # Since: 1.0
 ##
 { 'command': 'query-alarm-clock', 'returns': 'QemuAlarmClock' }

Notice the "returns" keyword. As its name suggests, it's used to define the
data returned by a command.

It's time to implement the qmp_query_alarm_clock() function, you can put it
in the qemu-timer.c file::

 QemuAlarmClock *qmp_query_alarm_clock(Error **errp)
 {
     QemuAlarmClock *clock;
     int64_t deadline;

     clock = g_malloc0(sizeof(*clock));

     deadline = qemu_next_alarm_deadline();
     if (deadline > 0) {
         clock->has_next_deadline = true;
         clock->next_deadline = deadline;
     }
     clock->clock_name = g_strdup(alarm_timer->name);

     return clock;
 }

There are a number of things to be noticed:

1. The QemuAlarmClock type is automatically generated by the QAPI framework,
   its members correspond to the type's specification in the schema file
2. As specified in the schema file, the function returns a QemuAlarmClock
   instance and takes no arguments (besides the "errp" one, which is mandatory
   for all QMP functions)
3. The "clock" variable (which will point to our QAPI type instance) is
   allocated by the regular g_malloc0() function. Note that we chose to
   initialize the memory to zero. This is recommended for all QAPI types, as
   it helps avoiding bad surprises (specially with booleans)
4. Remember that "next_deadline" is optional? All optional members have a
   'has_TYPE_NAME' member that should be properly set by the implementation,
   as shown above
5. Even static strings, such as "alarm_timer->name", should be dynamically
   allocated by the implementation. This is so because the QAPI also generates
   a function to free its types and it cannot distinguish between dynamically
   or statically allocated strings
6. You have to include "qapi/qapi-commands-misc.h" in qemu-timer.c

Time to test the new command. Build qemu, run it as described in the "Testing"
section and try this::

 { "execute": "query-alarm-clock" }
 {
     "return": {
         "next-deadline": 2368219,
         "clock-name": "dynticks"
     }
 }


The HMP command
~~~~~~~~~~~~~~~

Here's the HMP counterpart of the query-alarm-clock command::

 void hmp_info_alarm_clock(Monitor *mon)
 {
     QemuAlarmClock *clock;
     Error *err = NULL;

     clock = qmp_query_alarm_clock(&err);
     if (hmp_handle_error(mon, err)) {
         return;
     }

     monitor_printf(mon, "Alarm clock method in use: '%s'\n", clock->clock_name);
     if (clock->has_next_deadline) {
         monitor_printf(mon, "Next alarm will fire in %" PRId64 " nanoseconds\n",
                        clock->next_deadline);
     }

    qapi_free_QemuAlarmClock(clock);
 }

It's important to notice that hmp_info_alarm_clock() calls
qapi_free_QemuAlarmClock() to free the data returned by qmp_query_alarm_clock().
For user defined types, the QAPI will generate a qapi_free_QAPI_TYPE_NAME()
function and that's what you have to use to free the types you define and
qapi_free_QAPI_TYPE_NAMEList() for list types (explained in the next section).
If the QMP call returns a string, then you should g_free() to free it.

Also note that hmp_info_alarm_clock() performs error handling. That's not
strictly required if you're sure the QMP function doesn't return errors, but
it's good practice to always check for errors.

Another important detail is that HMP's "info" commands don't go into the
hmp-commands.hx. Instead, they go into the info_cmds[] table, which is defined
in the monitor/misc.c file. The entry for the "info alarmclock" follows::

    {
        .name       = "alarmclock",
        .args_type  = "",
        .params     = "",
        .help       = "show information about the alarm clock",
        .cmd        = hmp_info_alarm_clock,
    },

To test this, run qemu and type "info alarmclock" in the user monitor.


Returning Lists
~~~~~~~~~~~~~~~

For this example, we're going to return all available methods for the timer
alarm, which is pretty much what the command-line option "-clock ?" does,
except that we're also going to inform which method is in use.

This first step is to define a new type::

 ##
 # @TimerAlarmMethod
 #
 # Timer alarm method information.
 #
 # @method-name: The method's name.
 #
 # @current: true if this alarm method is currently in use, false otherwise
 #
 # Since: 1.0
 ##
 { 'type': 'TimerAlarmMethod',
   'data': { 'method-name': 'str', 'current': 'bool' } }

The command will be called "query-alarm-methods", here is its schema
specification::

 ##
 # @query-alarm-methods
 #
 # Returns information about available alarm methods.
 #
 # Returns: a list of @TimerAlarmMethod for each method
 #
 # Since: 1.0
 ##
 { 'command': 'query-alarm-methods', 'returns': ['TimerAlarmMethod'] }

Notice the syntax for returning lists "'returns': ['TimerAlarmMethod']", this
should be read as "returns a list of TimerAlarmMethod instances".

The C implementation follows::

 TimerAlarmMethodList *qmp_query_alarm_methods(Error **errp)
 {
     TimerAlarmMethodList *method_list = NULL;
     const struct qemu_alarm_timer *p;
     bool current = true;

     for (p = alarm_timers; p->name; p++) {
         TimerAlarmMethod *value = g_malloc0(*value);
         value->method_name = g_strdup(p->name);
         value->current = current;
         QAPI_LIST_PREPEND(method_list, value);
         current = false;
     }

     return method_list;
 }

The most important difference from the previous examples is the
TimerAlarmMethodList type, which is automatically generated by the QAPI from
the TimerAlarmMethod type.

Each list node is represented by a TimerAlarmMethodList instance. We have to
allocate it, and that's done inside the for loop: the "info" pointer points to
an allocated node. We also have to allocate the node's contents, which is
stored in its "value" member. In our example, the "value" member is a pointer
to an TimerAlarmMethod instance.

Notice that the "current" variable is used as "true" only in the first
iteration of the loop. That's because the alarm timer method in use is the
first element of the alarm_timers array. Also notice that QAPI lists are handled
by hand and we return the head of the list.

Now Build qemu, run it as explained in the "Testing" section and try our new
command::

 { "execute": "query-alarm-methods" }
 {
     "return": [
         {
             "current": false,
             "method-name": "unix"
         },
         {
             "current": true,
             "method-name": "dynticks"
         }
     ]
 }

The HMP counterpart is a bit more complex than previous examples because it
has to traverse the list, it's shown below for reference::

 void hmp_info_alarm_methods(Monitor *mon)
 {
     TimerAlarmMethodList *method_list, *method;
     Error *err = NULL;

     method_list = qmp_query_alarm_methods(&err);
     if (hmp_handle_error(mon, err)) {
         return;
     }

     for (method = method_list; method; method = method->next) {
         monitor_printf(mon, "%c %s\n", method->value->current ? '*' : ' ',
                                        method->value->method_name);
     }

     qapi_free_TimerAlarmMethodList(method_list);
 }

Writing a debugging aid returning unstructured text
---------------------------------------------------

As discussed in section `Modelling data in QAPI`_, it is required that
commands expecting machine usage be using fine-grained QAPI data types.
The exception to this rule applies when the command is solely intended
as a debugging aid and allows for returning unstructured text. This is
commonly needed for query commands that report aspects of QEMU's
internal state that are useful to human operators.

In this example we will consider a simplified variant of the HMP
command ``info roms``. Following the earlier rules, this command will
need to live under the ``x-`` name prefix, so its QMP implementation
will be called ``x-query-roms``. It will have no parameters and will
return a single text string::

 { 'struct': 'HumanReadableText',
   'data': { 'human-readable-text': 'str' } }

 { 'command': 'x-query-roms',
   'returns': 'HumanReadableText' }

The ``HumanReadableText`` struct is intended to be used for all
commands, under the ``x-`` name prefix that are returning unstructured
text targeted at humans. It should never be used for commands outside
the ``x-`` name prefix, as those should be using structured QAPI types.

Implementing the QMP command
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The QMP implementation will typically involve creating a ``GString``
object and printing formatted data into it::

 HumanReadableText *qmp_x_query_roms(Error **errp)
 {
     g_autoptr(GString) buf = g_string_new("");
     Rom *rom;

     QTAILQ_FOREACH(rom, &roms, next) {
        g_string_append_printf("%s size=0x%06zx name=\"%s\"\n",
                               memory_region_name(rom->mr),
                               rom->romsize,
                               rom->name);
     }

     return human_readable_text_from_str(buf);
 }


Implementing the HMP command
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Now that the QMP command is in place, we can also make it available in
the human monitor (HMP) as shown in previous examples. The HMP
implementations will all look fairly similar, as all they need do is
invoke the QMP command and then print the resulting text or error
message. Here's the implementation of the "info roms" HMP command::

 void hmp_info_roms(Monitor *mon, const QDict *qdict)
 {
     Error err = NULL;
     g_autoptr(HumanReadableText) info = qmp_x_query_roms(&err);

     if (hmp_handle_error(mon, err)) {
         return;
     }
     monitor_puts(mon, info->human_readable_text);
 }

Also, you have to add the function's prototype to the hmp.h file.

There's one last step to actually make the command available to
monitor users, we should add it to the hmp-commands-info.hx file::

    {
        .name       = "roms",
        .args_type  = "",
        .params     = "",
        .help       = "show roms",
        .cmd        = hmp_info_roms,
    },

The case of writing a HMP info handler that calls a no-parameter QMP query
command is quite common. To simplify the implementation there is a general
purpose HMP info handler for this scenario. All that is required to expose
a no-parameter QMP query command via HMP is to declare it using the
'.cmd_info_hrt' field to point to the QMP handler, and leave the '.cmd'
field NULL::

    {
        .name         = "roms",
        .args_type    = "",
        .params       = "",
        .help         = "show roms",
        .cmd_info_hrt = qmp_x_query_roms,
    },
