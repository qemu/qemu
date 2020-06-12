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

#ifndef QIO_TASK_H
#define QIO_TASK_H

typedef struct QIOTask QIOTask;

typedef void (*QIOTaskFunc)(QIOTask *task,
                            gpointer opaque);

typedef void (*QIOTaskWorker)(QIOTask *task,
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
 *   <title>Task function signature</title>
 *   <programlisting>
 *  void myobject_operation(QMyObject *obj,
 *                          QIOTaskFunc *func,
 *                          gpointer opaque,
 *                          GDestroyNotify notify);
 *   </programlisting>
 * </example>
 *
 * The 'func' parameter is the callback to be invoked, and 'opaque'
 * is data to pass to it. The optional 'notify' function is used
 * to free 'opaque' when no longer needed.
 *
 * When the operation completes, the 'func' callback will be
 * invoked, allowing the calling code to determine the result
 * of the operation. An example QIOTaskFunc implementation may
 * look like
 *
 * <example>
 *   <title>Task callback implementation</title>
 *   <programlisting>
 *  static void myobject_operation_notify(QIOTask *task,
 *                                        gpointer opaque)
 *  {
 *      Error *err = NULL;
 *      if (qio_task_propagate_error(task, &err)) {
 *          ...deal with the failure...
 *          error_free(err);
 *      } else {
 *          QMyObject *src = QMY_OBJECT(qio_task_get_source(task));
 *          ...deal with the completion...
 *      }
 *  }
 *   </programlisting>
 * </example>
 *
 * Now, lets say the implementation of the method using the
 * task wants to set a timer to run once a second checking
 * for completion of some activity. It would do something
 * like
 *
 * <example>
 *   <title>Task function implementation</title>
 *   <programlisting>
 *    void myobject_operation(QMyObject *obj,
 *                            QIOTaskFunc *func,
 *                            gpointer opaque,
 *                            GDestroyNotify notify)
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
 *      Error *err = NULL;
 *
 *      ...check something important...
 *       if (err) {
 *           qio_task_set_error(task, err);
 *           qio_task_complete(task);
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
 * The 'qio_task_complete' call in this method will trigger
 * the callback func 'myobject_operation_notify' shown
 * earlier to deal with the results.
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
 *    static void myobject_listen_worker(QIOTask *task,
 *                                       gpointer opaque)
 *    {
 *       QMyObject obj = QMY_OBJECT(qio_task_get_source(task));
 *       SocketAddress *addr = opaque;
 *       Error *err = NULL;
 *
 *       obj->fd = socket_listen(addr, &err);
 *
         qio_task_set_error(task, err);
 *    }
 *
 *    void myobject_listen_async(QMyObject *obj,
 *                               SocketAddress *addr,
 *                               QIOTaskFunc *func,
 *                               gpointer opaque,
 *                               GDestroyNotify notify)
 *    {
 *      QIOTask *task;
 *      SocketAddress *addrCopy;
 *
 *      addrCopy = QAPI_CLONE(SocketAddress, addr);
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
 * The returned task will be released when qio_task_complete()
 * is invoked.
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
 * @context: the context to run the complete hook. If %NULL, the
 *           default context will be used.
 *
 * Run a task in a background thread. When @worker
 * returns it will call qio_task_complete() in
 * the thread that is running the main loop associated
 * with @context.
 */
void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy,
                            GMainContext *context);


/**
 * qio_task_wait_thread:
 * @task: the task struct
 *
 * Wait for completion of a task that was previously
 * invoked using qio_task_run_in_thread. This MUST
 * ONLY be invoked if the task has not already
 * completed, since after the completion callback
 * is invoked, @task will have been freed.
 *
 * To avoid racing with execution of the completion
 * callback provided with qio_task_new, this method
 * MUST ONLY be invoked from the thread that is
 * running the main loop associated with @context
 * parameter to qio_task_run_in_thread.
 *
 * When the thread has completed, the completion
 * callback provided to qio_task_new will be invoked.
 * When that callback returns @task will be freed,
 * so @task must not be referenced after this
 * method completes.
 */
void qio_task_wait_thread(QIOTask *task);


/**
 * qio_task_complete:
 * @task: the task struct
 *
 * Invoke the completion callback for @task and
 * then free its memory.
 */
void qio_task_complete(QIOTask *task);


/**
 * qio_task_set_error:
 * @task: the task struct
 * @err: pointer to the error, or NULL
 *
 * Associate an error with the task, which can later
 * be retrieved with the qio_task_propagate_error()
 * method. This method takes ownership of @err, so
 * it is not valid to access it after this call
 * completes. If @err is NULL this is a no-op. If
 * this is call multiple times, only the first
 * provided @err will be recorded, later ones will
 * be discarded and freed.
 */
void qio_task_set_error(QIOTask *task,
                        Error *err);


/**
 * qio_task_propagate_error:
 * @task: the task struct
 * @errp: pointer to a NULL-initialized error object
 *
 * Propagate the error associated with @task
 * into @errp.
 *
 * Returns: true if an error was propagated, false otherwise
 */
bool qio_task_propagate_error(QIOTask *task,
                              Error **errp);


/**
 * qio_task_set_result_pointer:
 * @task: the task struct
 * @result: pointer to the result data
 *
 * Associate an opaque result with the task,
 * which can later be retrieved with the
 * qio_task_get_result_pointer() method
 *
 */
void qio_task_set_result_pointer(QIOTask *task,
                                 gpointer result,
                                 GDestroyNotify notify);


/**
 * qio_task_get_result_pointer:
 * @task: the task struct
 *
 * Retrieve the opaque result data associated
 * with the task, if any.
 *
 * Returns: the task result, or NULL
 */
gpointer qio_task_get_result_pointer(QIOTask *task);


/**
 * qio_task_get_source:
 * @task: the task struct
 *
 * Get the source object associated with the background
 * task. The caller does not own a reference on the
 * returned Object, and so should call object_ref()
 * if it wants to keep the object pointer outside the
 * lifetime of the QIOTask object.
 *
 * Returns: the source object
 */
Object *qio_task_get_source(QIOTask *task);

#endif /* QIO_TASK_H */
