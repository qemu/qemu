/*
 * QEMU I/O task
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QIO_TASK_H__
#define QIO_TASK_H__

#include "qemu-common.h"
#include "qom/object.h"

typedef struct QIOTask QIOTask;

typedef void (*QIOTaskFunc)(Object *source,
                            Error *err,
                            gpointer opaque);

typedef int (*QIOTaskWorker)(QIOTask *task,
                             Error **errp,
                             gpointer opaque);

/**
 * QIOTask:
 *
 * The QIOTask object provides a simple mechanism for reporting
 * success / failure of long running background operations.
 *
 * A object on which the operation is to be performed could have
 * a public API which accepts a task callback:
 *
 * <example>
 *   <title>Task callback function signature</title>
 *   <programlisting>
 *  void myobject_operation(QMyObject *obj,
 *                          QIOTaskFunc *func,
 *                          gpointer opaque,
 *                          GDestroyNotify *notify);
 *   </programlisting>
 * </example>
 *
 * The 'func' parameter is the callback to be invoked, and 'opaque'
 * is data to pass to it. The optional 'notify' function is used
 * to free 'opaque' when no longer needed.
 *
 * Now, lets say the implementation of this method wants to set
 * a timer to run once a second checking for completion of some
 * activity. It would do something like
 *
 * <example>
 *   <title>Task callback function implementation</title>
 *   <programlisting>
 *    void myobject_operation(QMyObject *obj,
 *                            QIOTaskFunc *func,
 *                            gpointer opaque,
 *                            GDestroyNotify *notify)
 *    {
 *      QIOTask *task;
 *
 *      task = qio_task_new(OBJECT(obj), func, opaque, notify);
 *
 *      g_timeout_add_full(G_PRIORITY_DEFAULT,
 *                         1000,
 *                         myobject_operation_timer,
 *                         task,
 *                         NULL);
 *    }
 *   </programlisting>
 * </example>
 *
 * It could equally have setup a watch on a file descriptor or
 * created a background thread, or something else entirely.
 * Notice that the source object is passed to the task, and
 * QIOTask will hold a reference on that. This ensure that
 * the QMyObject instance cannot be garbage collected while
 * the async task is still in progress.
 *
 * In this case, myobject_operation_timer will fire after
 * 3 secs and do
 *
 * <example>
 *   <title>Task timer function</title>
 *   <programlisting>
 *   gboolean myobject_operation_timer(gpointer opaque)
 *   {
 *      QIOTask *task = QIO_TASK(opaque);
 *      Error *err;*
 *
 *      ...check something important...
 *       if (err) {
 *           qio_task_abort(task, err);
 *           error_free(task);
 *           return FALSE;
 *       } else if (...work is completed ...) {
 *           qio_task_complete(task);
 *           return FALSE;
 *       }
 *       ...carry on polling ...
 *       return TRUE;
 *   }
 *   </programlisting>
 * </example>
 *
 * Once this function returns false, object_unref will be called
 * automatically on the task causing it to be released and the
 * ref on QMyObject dropped too.
 *
 * The QIOTask module can also be used to perform operations
 * in a background thread context, while still reporting the
 * results in the main event thread. This allows code which
 * cannot easily be rewritten to be asychronous (such as DNS
 * lookups) to be easily run non-blocking. Reporting the
 * results in the main thread context means that the caller
 * typically does not need to be concerned about thread
 * safety wrt the QEMU global mutex.
 *
 * For example, the socket_listen() method will block the caller
 * while DNS lookups take place if given a name, instead of IP
 * address. The C library often do not provide a practical async
 * DNS API, so the to get non-blocking DNS lookups in a portable
 * manner requires use of a thread. So achieve a non-blocking
 * socket listen using QIOTask would require:
 *
 * <example>
 *    static int myobject_listen_worker(QIOTask *task,
 *                                      Error **errp,
 *                                      gpointer opaque)
 *    {
 *       QMyObject obj = QMY_OBJECT(qio_task_get_source(task));
 *       SocketAddress *addr = opaque;
 *
 *       obj->fd = socket_listen(addr, errp);
 *       if (obj->fd < 0) {
 *          return -1;
 *       }
 *       return 0;
 *    }
 *
 *    void myobject_listen_async(QMyObject *obj,
 *                               SocketAddress *addr,
 *                               QIOTaskFunc *func,
 *                               gpointer opaque,
 *                               GDestroyNotify *notify)
 *    {
 *      QIOTask *task;
 *      SocketAddress *addrCopy;
 *
 *      qapi_copy_SocketAddress(&addrCopy, addr);
 *      task = qio_task_new(OBJECT(obj), func, opaque, notify);
 *
 *      qio_task_run_in_thread(task, myobject_listen_worker,
 *                             addrCopy,
 *                             qapi_free_SocketAddress);
 *    }
 * </example>
 *
 * NB, The 'func' callback passed into myobject_listen_async
 * will be invoked from the main event thread, despite the
 * actual operation being performed in a different thread.
 */

/**
 * qio_task_new:
 * @source: the object on which the operation is invoked
 * @func: the callback to invoke when the task completes
 * @opaque: opaque data to pass to @func when invoked
 * @destroy: optional callback to free @opaque
 *
 * Creates a new task struct to track completion of a
 * background operation running on the object @source.
 * When the operation completes or fails, the callback
 * @func will be invoked. The callback can access the
 * 'err' attribute in the task object to determine if
 * the operation was successful or not.
 *
 * The returned task will be released when one of
 * qio_task_abort() or qio_task_complete() are invoked.
 *
 * Returns: the task struct
 */
QIOTask *qio_task_new(Object *source,
                      QIOTaskFunc func,
                      gpointer opaque,
                      GDestroyNotify destroy);

/**
 * qio_task_run_in_thread:
 * @task: the task struct
 * @worker: the function to invoke in a thread
 * @opaque: opaque data to pass to @worker
 * @destroy: function to free @opaque
 *
 * Run a task in a background thread. If @worker
 * returns 0 it will call qio_task_complete() in
 * the main event thread context. If @worker
 * returns -1 it will call qio_task_abort() in
 * the main event thread context.
 */
void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy);

/**
 * qio_task_complete:
 * @task: the task struct
 *
 * Mark the operation as succesfully completed
 * and free the memory for @task.
 */
void qio_task_complete(QIOTask *task);

/**
 * qio_task_abort:
 * @task: the task struct
 * @err: the error to record for the operation
 *
 * Mark the operation as failed, with @err providing
 * details about the failure. The @err may be freed
 * afer the function returns, as the notification
 * callback is invoked synchronously. The @task will
 * be freed when this call completes.
 */
void qio_task_abort(QIOTask *task,
                    Error *err);


/**
 * qio_task_get_source:
 * @task: the task struct
 *
 * Get the source object associated with the background
 * task. This returns a new reference to the object,
 * which the caller must released with object_unref()
 * when no longer required.
 *
 * Returns: the source object
 */
Object *qio_task_get_source(QIOTask *task);

#endif /* QIO_TASK_H__ */
