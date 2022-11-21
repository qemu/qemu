/*
 * QEMU I/O channels
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef QIO_CHANNEL_H
#define QIO_CHANNEL_H

#include "qom/object.h"
#include "qemu/coroutine.h"
#include "block/aio.h"

#define TYPE_QIO_CHANNEL "qio-channel"
OBJECT_DECLARE_TYPE(QIOChannel, QIOChannelClass,
                    QIO_CHANNEL)


#define QIO_CHANNEL_ERR_BLOCK -2

#define QIO_CHANNEL_WRITE_FLAG_ZERO_COPY 0x1

typedef enum QIOChannelFeature QIOChannelFeature;

enum QIOChannelFeature {
    QIO_CHANNEL_FEATURE_FD_PASS,
    QIO_CHANNEL_FEATURE_SHUTDOWN,
    QIO_CHANNEL_FEATURE_LISTEN,
    QIO_CHANNEL_FEATURE_WRITE_ZERO_COPY,
};


typedef enum QIOChannelShutdown QIOChannelShutdown;

enum QIOChannelShutdown {
    QIO_CHANNEL_SHUTDOWN_READ = 1,
    QIO_CHANNEL_SHUTDOWN_WRITE = 2,
    QIO_CHANNEL_SHUTDOWN_BOTH = 3,
};

typedef gboolean (*QIOChannelFunc)(QIOChannel *ioc,
                                   GIOCondition condition,
                                   gpointer data);

/**
 * QIOChannel:
 *
 * The QIOChannel defines the core API for a generic I/O channel
 * class hierarchy. It is inspired by GIOChannel, but has the
 * following differences
 *
 *  - Use QOM to properly support arbitrary subclassing
 *  - Support use of iovecs for efficient I/O with multiple blocks
 *  - None of the character set translation, binary data exclusively
 *  - Direct support for QEMU Error object reporting
 *  - File descriptor passing
 *
 * This base class is abstract so cannot be instantiated. There
 * will be subclasses for dealing with sockets, files, and higher
 * level protocols such as TLS, WebSocket, etc.
 */

struct QIOChannel {
    Object parent;
    unsigned int features; /* bitmask of QIOChannelFeatures */
    char *name;
    AioContext *ctx;
    Coroutine *read_coroutine;
    Coroutine *write_coroutine;
#ifdef _WIN32
    HANDLE event; /* For use with GSource on Win32 */
#endif
};

/**
 * QIOChannelClass:
 *
 * This class defines the contract that all subclasses
 * must follow to provide specific channel implementations.
 * The first five callbacks are mandatory to support, others
 * provide additional optional features.
 *
 * Consult the corresponding public API docs for a description
 * of the semantics of each callback. io_shutdown in particular
 * must be thread-safe, terminate quickly and must not block.
 */
struct QIOChannelClass {
    ObjectClass parent;

    /* Mandatory callbacks */
    ssize_t (*io_writev)(QIOChannel *ioc,
                         const struct iovec *iov,
                         size_t niov,
                         int *fds,
                         size_t nfds,
                         int flags,
                         Error **errp);
    ssize_t (*io_readv)(QIOChannel *ioc,
                        const struct iovec *iov,
                        size_t niov,
                        int **fds,
                        size_t *nfds,
                        Error **errp);
    int (*io_close)(QIOChannel *ioc,
                    Error **errp);
    GSource * (*io_create_watch)(QIOChannel *ioc,
                                 GIOCondition condition);
    int (*io_set_blocking)(QIOChannel *ioc,
                           bool enabled,
                           Error **errp);

    /* Optional callbacks */
    int (*io_shutdown)(QIOChannel *ioc,
                       QIOChannelShutdown how,
                       Error **errp);
    void (*io_set_cork)(QIOChannel *ioc,
                        bool enabled);
    void (*io_set_delay)(QIOChannel *ioc,
                         bool enabled);
    off_t (*io_seek)(QIOChannel *ioc,
                     off_t offset,
                     int whence,
                     Error **errp);
    void (*io_set_aio_fd_handler)(QIOChannel *ioc,
                                  AioContext *ctx,
                                  IOHandler *io_read,
                                  IOHandler *io_write,
                                  void *opaque);
    int (*io_flush)(QIOChannel *ioc,
                    Error **errp);
};

/* General I/O handling functions */

/**
 * qio_channel_has_feature:
 * @ioc: the channel object
 * @feature: the feature to check support of
 *
 * Determine whether the channel implementation supports
 * the optional feature named in @feature.
 *
 * Returns: true if supported, false otherwise.
 */
bool qio_channel_has_feature(QIOChannel *ioc,
                             QIOChannelFeature feature);

/**
 * qio_channel_set_feature:
 * @ioc: the channel object
 * @feature: the feature to set support for
 *
 * Add channel support for the feature named in @feature.
 */
void qio_channel_set_feature(QIOChannel *ioc,
                             QIOChannelFeature feature);

/**
 * qio_channel_set_name:
 * @ioc: the channel object
 * @name: the name of the channel
 *
 * Sets the name of the channel, which serves as an aid
 * to debugging. The name is used when creating GSource
 * watches for this channel.
 */
void qio_channel_set_name(QIOChannel *ioc,
                          const char *name);

/**
 * qio_channel_readv_full:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data into
 * @niov: the length of the @iov array
 * @fds: pointer to an array that will received file handles
 * @nfds: pointer filled with number of elements in @fds on return
 * @errp: pointer to a NULL-initialized error object
 *
 * Read data from the IO channel, storing it in the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully populated with data
 * before the next one is used. The @niov parameter
 * specifies the total number of elements in @iov.
 *
 * It is not required for all @iov to be filled with
 * data. If the channel is in blocking mode, at least
 * one byte of data will be read, but no more is
 * guaranteed. If the channel is non-blocking and no
 * data is available, it will return QIO_CHANNEL_ERR_BLOCK
 *
 * If the channel has passed any file descriptors,
 * the @fds array pointer will be allocated and
 * the elements filled with the received file
 * descriptors. The @nfds pointer will be updated
 * to indicate the size of the @fds array that
 * was allocated. It is the callers responsibility
 * to call close() on each file descriptor and to
 * call g_free() on the array pointer in @fds.
 *
 * It is an error to pass a non-NULL @fds parameter
 * unless qio_channel_has_feature() returns a true
 * value for the QIO_CHANNEL_FEATURE_FD_PASS constant.
 *
 * Returns: the number of bytes read, or -1 on error,
 * or QIO_CHANNEL_ERR_BLOCK if no data is available
 * and the channel is non-blocking
 */
ssize_t qio_channel_readv_full(QIOChannel *ioc,
                               const struct iovec *iov,
                               size_t niov,
                               int **fds,
                               size_t *nfds,
                               Error **errp);


/**
 * qio_channel_writev_full:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @fds: an array of file handles to send
 * @nfds: number of file handles in @fds
 * @flags: write flags (QIO_CHANNEL_WRITE_FLAG_*)
 * @errp: pointer to a NULL-initialized error object
 *
 * Write data to the IO channel, reading it from the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully sent, before the next
 * one is used. The @niov parameter specifies the
 * total number of elements in @iov.
 *
 * It is not required for all @iov data to be fully
 * sent. If the channel is in blocking mode, at least
 * one byte of data will be sent, but no more is
 * guaranteed. If the channel is non-blocking and no
 * data can be sent, it will return QIO_CHANNEL_ERR_BLOCK
 *
 * If there are file descriptors to send, the @fds
 * array should be non-NULL and provide the handles.
 * All file descriptors will be sent if at least one
 * byte of data was sent.
 *
 * It is an error to pass a non-NULL @fds parameter
 * unless qio_channel_has_feature() returns a true
 * value for the QIO_CHANNEL_FEATURE_FD_PASS constant.
 *
 * Returns: the number of bytes sent, or -1 on error,
 * or QIO_CHANNEL_ERR_BLOCK if no data is can be sent
 * and the channel is non-blocking
 */
ssize_t qio_channel_writev_full(QIOChannel *ioc,
                                const struct iovec *iov,
                                size_t niov,
                                int *fds,
                                size_t nfds,
                                int flags,
                                Error **errp);

/**
 * qio_channel_readv_all_eof:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data into
 * @niov: the length of the @iov array
 * @errp: pointer to a NULL-initialized error object
 *
 * Read data from the IO channel, storing it in the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully populated with data
 * before the next one is used. The @niov parameter
 * specifies the total number of elements in @iov.
 *
 * The function will wait for all requested data
 * to be read, yielding from the current coroutine
 * if required.
 *
 * If end-of-file occurs before any data is read,
 * no error is reported; otherwise, if it occurs
 * before all requested data has been read, an error
 * will be reported.
 *
 * Returns: 1 if all bytes were read, 0 if end-of-file
 *          occurs without data, or -1 on error
 */
int qio_channel_readv_all_eof(QIOChannel *ioc,
                              const struct iovec *iov,
                              size_t niov,
                              Error **errp);

/**
 * qio_channel_readv_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data into
 * @niov: the length of the @iov array
 * @errp: pointer to a NULL-initialized error object
 *
 * Read data from the IO channel, storing it in the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully populated with data
 * before the next one is used. The @niov parameter
 * specifies the total number of elements in @iov.
 *
 * The function will wait for all requested data
 * to be read, yielding from the current coroutine
 * if required.
 *
 * If end-of-file occurs before all requested data
 * has been read, an error will be reported.
 *
 * Returns: 0 if all bytes were read, or -1 on error
 */
int qio_channel_readv_all(QIOChannel *ioc,
                          const struct iovec *iov,
                          size_t niov,
                          Error **errp);


/**
 * qio_channel_writev_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @errp: pointer to a NULL-initialized error object
 *
 * Write data to the IO channel, reading it from the
 * memory regions referenced by @iov. Each element
 * in the @iov will be fully sent, before the next
 * one is used. The @niov parameter specifies the
 * total number of elements in @iov.
 *
 * The function will wait for all requested data
 * to be written, yielding from the current coroutine
 * if required.
 *
 * Returns: 0 if all bytes were written, or -1 on error
 */
int qio_channel_writev_all(QIOChannel *ioc,
                           const struct iovec *iov,
                           size_t niov,
                           Error **errp);

/**
 * qio_channel_readv:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data into
 * @niov: the length of the @iov array
 * @errp: pointer to a NULL-initialized error object
 *
 * Behaves as qio_channel_readv_full() but does not support
 * receiving of file handles.
 */
ssize_t qio_channel_readv(QIOChannel *ioc,
                          const struct iovec *iov,
                          size_t niov,
                          Error **errp);

/**
 * qio_channel_writev:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @errp: pointer to a NULL-initialized error object
 *
 * Behaves as qio_channel_writev_full() but does not support
 * sending of file handles.
 */
ssize_t qio_channel_writev(QIOChannel *ioc,
                           const struct iovec *iov,
                           size_t niov,
                           Error **errp);

/**
 * qio_channel_read:
 * @ioc: the channel object
 * @buf: the memory region to read data into
 * @buflen: the length of @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Behaves as qio_channel_readv_full() but does not support
 * receiving of file handles, and only supports reading into
 * a single memory region.
 */
ssize_t qio_channel_read(QIOChannel *ioc,
                         char *buf,
                         size_t buflen,
                         Error **errp);

/**
 * qio_channel_write:
 * @ioc: the channel object
 * @buf: the memory regions to send data from
 * @buflen: the length of @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Behaves as qio_channel_writev_full() but does not support
 * sending of file handles, and only supports writing from a
 * single memory region.
 */
ssize_t qio_channel_write(QIOChannel *ioc,
                          const char *buf,
                          size_t buflen,
                          Error **errp);

/**
 * qio_channel_read_all_eof:
 * @ioc: the channel object
 * @buf: the memory region to read data into
 * @buflen: the number of bytes to @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Reads @buflen bytes into @buf, possibly blocking or (if the
 * channel is non-blocking) yielding from the current coroutine
 * multiple times until the entire content is read. If end-of-file
 * occurs immediately it is not an error, but if it occurs after
 * data has been read it will return an error rather than a
 * short-read. Otherwise behaves as qio_channel_read().
 *
 * Returns: 1 if all bytes were read, 0 if end-of-file occurs
 *          without data, or -1 on error
 */
int qio_channel_read_all_eof(QIOChannel *ioc,
                             char *buf,
                             size_t buflen,
                             Error **errp);

/**
 * qio_channel_read_all:
 * @ioc: the channel object
 * @buf: the memory region to read data into
 * @buflen: the number of bytes to @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Reads @buflen bytes into @buf, possibly blocking or (if the
 * channel is non-blocking) yielding from the current coroutine
 * multiple times until the entire content is read. If end-of-file
 * occurs it will return an error rather than a short-read. Otherwise
 * behaves as qio_channel_read().
 *
 * Returns: 0 if all bytes were read, or -1 on error
 */
int qio_channel_read_all(QIOChannel *ioc,
                         char *buf,
                         size_t buflen,
                         Error **errp);

/**
 * qio_channel_write_all:
 * @ioc: the channel object
 * @buf: the memory region to write data into
 * @buflen: the number of bytes to @buf
 * @errp: pointer to a NULL-initialized error object
 *
 * Writes @buflen bytes from @buf, possibly blocking or (if the
 * channel is non-blocking) yielding from the current coroutine
 * multiple times until the entire content is written.  Otherwise
 * behaves as qio_channel_write().
 *
 * Returns: 0 if all bytes were written, or -1 on error
 */
int qio_channel_write_all(QIOChannel *ioc,
                          const char *buf,
                          size_t buflen,
                          Error **errp);

/**
 * qio_channel_set_blocking:
 * @ioc: the channel object
 * @enabled: the blocking flag state
 * @errp: pointer to a NULL-initialized error object
 *
 * If @enabled is true, then the channel is put into
 * blocking mode, otherwise it will be non-blocking.
 *
 * In non-blocking mode, read/write operations may
 * return QIO_CHANNEL_ERR_BLOCK if they would otherwise
 * block on I/O
 */
int qio_channel_set_blocking(QIOChannel *ioc,
                             bool enabled,
                             Error **errp);

/**
 * qio_channel_close:
 * @ioc: the channel object
 * @errp: pointer to a NULL-initialized error object
 *
 * Close the channel, flushing any pending I/O
 *
 * Returns: 0 on success, -1 on error
 */
int qio_channel_close(QIOChannel *ioc,
                      Error **errp);

/**
 * qio_channel_shutdown:
 * @ioc: the channel object
 * @how: the direction to shutdown
 * @errp: pointer to a NULL-initialized error object
 *
 * Shutdowns transmission and/or receiving of data
 * without closing the underlying transport.
 *
 * Not all implementations will support this facility,
 * so may report an error. To avoid errors, the
 * caller may check for the feature flag
 * QIO_CHANNEL_FEATURE_SHUTDOWN prior to calling
 * this method.
 *
 * This function is thread-safe, terminates quickly and does not block.
 *
 * Returns: 0 on success, -1 on error
 */
int qio_channel_shutdown(QIOChannel *ioc,
                         QIOChannelShutdown how,
                         Error **errp);

/**
 * qio_channel_set_delay:
 * @ioc: the channel object
 * @enabled: the new flag state
 *
 * Controls whether the underlying transport is
 * permitted to delay writes in order to merge
 * small packets. If @enabled is true, then the
 * writes may be delayed in order to opportunistically
 * merge small packets into larger ones. If @enabled
 * is false, writes are dispatched immediately with
 * no delay.
 *
 * When @enabled is false, applications may wish to
 * use the qio_channel_set_cork() method to explicitly
 * control write merging.
 *
 * On channels which are backed by a socket, this
 * API corresponds to the inverse of TCP_NODELAY flag,
 * controlling whether the Nagle algorithm is active.
 *
 * This setting is merely a hint, so implementations are
 * free to ignore this without it being considered an
 * error.
 */
void qio_channel_set_delay(QIOChannel *ioc,
                           bool enabled);

/**
 * qio_channel_set_cork:
 * @ioc: the channel object
 * @enabled: the new flag state
 *
 * Controls whether the underlying transport is
 * permitted to dispatch data that is written.
 * If @enabled is true, then any data written will
 * be queued in local buffers until @enabled is
 * set to false once again.
 *
 * This feature is typically used when the automatic
 * write coalescing facility is disabled via the
 * qio_channel_set_delay() method.
 *
 * On channels which are backed by a socket, this
 * API corresponds to the TCP_CORK flag.
 *
 * This setting is merely a hint, so implementations are
 * free to ignore this without it being considered an
 * error.
 */
void qio_channel_set_cork(QIOChannel *ioc,
                          bool enabled);


/**
 * qio_channel_seek:
 * @ioc: the channel object
 * @offset: the position to seek to, relative to @whence
 * @whence: one of the (POSIX) SEEK_* constants listed below
 * @errp: pointer to a NULL-initialized error object
 *
 * Moves the current I/O position within the channel
 * @ioc, to be @offset. The value of @offset is
 * interpreted relative to @whence:
 *
 * SEEK_SET - the position is set to @offset bytes
 * SEEK_CUR - the position is moved by @offset bytes
 * SEEK_END - the position is set to end of the file plus @offset bytes
 *
 * Not all implementations will support this facility,
 * so may report an error.
 *
 * Returns: the new position on success, (off_t)-1 on failure
 */
off_t qio_channel_io_seek(QIOChannel *ioc,
                          off_t offset,
                          int whence,
                          Error **errp);


/**
 * qio_channel_create_watch:
 * @ioc: the channel object
 * @condition: the I/O condition to monitor
 *
 * Create a new main loop source that is used to watch
 * for the I/O condition @condition. Typically the
 * qio_channel_add_watch() method would be used instead
 * of this, since it directly attaches a callback to
 * the source
 *
 * Returns: the new main loop source.
 */
GSource *qio_channel_create_watch(QIOChannel *ioc,
                                  GIOCondition condition);

/**
 * qio_channel_add_watch:
 * @ioc: the channel object
 * @condition: the I/O condition to monitor
 * @func: callback to invoke when the source becomes ready
 * @user_data: opaque data to pass to @func
 * @notify: callback to free @user_data
 *
 * Create a new main loop source that is used to watch
 * for the I/O condition @condition. The callback @func
 * will be registered against the source, to be invoked
 * when the source becomes ready. The optional @user_data
 * will be passed to @func when it is invoked. The @notify
 * callback will be used to free @user_data when the
 * watch is deleted
 *
 * The returned source ID can be used with g_source_remove()
 * to remove and free the source when no longer required.
 * Alternatively the @func callback can return a FALSE
 * value.
 *
 * Returns: the source ID
 */
guint qio_channel_add_watch(QIOChannel *ioc,
                            GIOCondition condition,
                            QIOChannelFunc func,
                            gpointer user_data,
                            GDestroyNotify notify);

/**
 * qio_channel_add_watch_full:
 * @ioc: the channel object
 * @condition: the I/O condition to monitor
 * @func: callback to invoke when the source becomes ready
 * @user_data: opaque data to pass to @func
 * @notify: callback to free @user_data
 * @context: the context to run the watch source
 *
 * Similar as qio_channel_add_watch(), but allows to specify context
 * to run the watch source.
 *
 * Returns: the source ID
 */
guint qio_channel_add_watch_full(QIOChannel *ioc,
                                 GIOCondition condition,
                                 QIOChannelFunc func,
                                 gpointer user_data,
                                 GDestroyNotify notify,
                                 GMainContext *context);

/**
 * qio_channel_add_watch_source:
 * @ioc: the channel object
 * @condition: the I/O condition to monitor
 * @func: callback to invoke when the source becomes ready
 * @user_data: opaque data to pass to @func
 * @notify: callback to free @user_data
 * @context: gcontext to bind the source to
 *
 * Similar as qio_channel_add_watch(), but allows to specify context
 * to run the watch source, meanwhile return the GSource object
 * instead of tag ID, with the GSource referenced already.
 *
 * Note: callers is responsible to unref the source when not needed.
 *
 * Returns: the source pointer
 */
GSource *qio_channel_add_watch_source(QIOChannel *ioc,
                                      GIOCondition condition,
                                      QIOChannelFunc func,
                                      gpointer user_data,
                                      GDestroyNotify notify,
                                      GMainContext *context);

/**
 * qio_channel_attach_aio_context:
 * @ioc: the channel object
 * @ctx: the #AioContext to set the handlers on
 *
 * Request that qio_channel_yield() sets I/O handlers on
 * the given #AioContext.  If @ctx is %NULL, qio_channel_yield()
 * uses QEMU's main thread event loop.
 *
 * You can move a #QIOChannel from one #AioContext to another even if
 * I/O handlers are set for a coroutine.  However, #QIOChannel provides
 * no synchronization between the calls to qio_channel_yield() and
 * qio_channel_attach_aio_context().
 *
 * Therefore you should first call qio_channel_detach_aio_context()
 * to ensure that the coroutine is not entered concurrently.  Then,
 * while the coroutine has yielded, call qio_channel_attach_aio_context(),
 * and then aio_co_schedule() to place the coroutine on the new
 * #AioContext.  The calls to qio_channel_detach_aio_context()
 * and qio_channel_attach_aio_context() should be protected with
 * aio_context_acquire() and aio_context_release().
 */
void qio_channel_attach_aio_context(QIOChannel *ioc,
                                    AioContext *ctx);

/**
 * qio_channel_detach_aio_context:
 * @ioc: the channel object
 *
 * Disable any I/O handlers set by qio_channel_yield().  With the
 * help of aio_co_schedule(), this allows moving a coroutine that was
 * paused by qio_channel_yield() to another context.
 */
void qio_channel_detach_aio_context(QIOChannel *ioc);

/**
 * qio_channel_yield:
 * @ioc: the channel object
 * @condition: the I/O condition to wait for
 *
 * Yields execution from the current coroutine until the condition
 * indicated by @condition becomes available.  @condition must
 * be either %G_IO_IN or %G_IO_OUT; it cannot contain both.  In
 * addition, no two coroutine can be waiting on the same condition
 * and channel at the same time.
 *
 * This must only be called from coroutine context. It is safe to
 * reenter the coroutine externally while it is waiting; in this
 * case the function will return even if @condition is not yet
 * available.
 */
void coroutine_fn qio_channel_yield(QIOChannel *ioc,
                                    GIOCondition condition);

/**
 * qio_channel_wait:
 * @ioc: the channel object
 * @condition: the I/O condition to wait for
 *
 * Block execution from the current thread until
 * the condition indicated by @condition becomes
 * available.
 *
 * This will enter a nested event loop to perform
 * the wait.
 */
void qio_channel_wait(QIOChannel *ioc,
                      GIOCondition condition);

/**
 * qio_channel_set_aio_fd_handler:
 * @ioc: the channel object
 * @ctx: the AioContext to set the handlers on
 * @io_read: the read handler
 * @io_write: the write handler
 * @opaque: the opaque value passed to the handler
 *
 * This is used internally by qio_channel_yield().  It can
 * be used by channel implementations to forward the handlers
 * to another channel (e.g. from #QIOChannelTLS to the
 * underlying socket).
 */
void qio_channel_set_aio_fd_handler(QIOChannel *ioc,
                                    AioContext *ctx,
                                    IOHandler *io_read,
                                    IOHandler *io_write,
                                    void *opaque);

/**
 * qio_channel_readv_full_all_eof:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data to
 * @niov: the length of the @iov array
 * @fds: an array of file handles to read
 * @nfds: number of file handles in @fds
 * @errp: pointer to a NULL-initialized error object
 *
 *
 * Performs same function as qio_channel_readv_all_eof.
 * Additionally, attempts to read file descriptors shared
 * over the channel. The function will wait for all
 * requested data to be read, yielding from the current
 * coroutine if required. data refers to both file
 * descriptors and the iovs.
 *
 * Returns: 1 if all bytes were read, 0 if end-of-file
 *          occurs without data, or -1 on error
 */

int qio_channel_readv_full_all_eof(QIOChannel *ioc,
                                   const struct iovec *iov,
                                   size_t niov,
                                   int **fds, size_t *nfds,
                                   Error **errp);

/**
 * qio_channel_readv_full_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to read data to
 * @niov: the length of the @iov array
 * @fds: an array of file handles to read
 * @nfds: number of file handles in @fds
 * @errp: pointer to a NULL-initialized error object
 *
 *
 * Performs same function as qio_channel_readv_all_eof.
 * Additionally, attempts to read file descriptors shared
 * over the channel. The function will wait for all
 * requested data to be read, yielding from the current
 * coroutine if required. data refers to both file
 * descriptors and the iovs.
 *
 * Returns: 0 if all bytes were read, or -1 on error
 */

int qio_channel_readv_full_all(QIOChannel *ioc,
                               const struct iovec *iov,
                               size_t niov,
                               int **fds, size_t *nfds,
                               Error **errp);

/**
 * qio_channel_writev_full_all:
 * @ioc: the channel object
 * @iov: the array of memory regions to write data from
 * @niov: the length of the @iov array
 * @fds: an array of file handles to send
 * @nfds: number of file handles in @fds
 * @flags: write flags (QIO_CHANNEL_WRITE_FLAG_*)
 * @errp: pointer to a NULL-initialized error object
 *
 *
 * Behaves like qio_channel_writev_full but will attempt
 * to send all data passed (file handles and memory regions).
 * The function will wait for all requested data
 * to be written, yielding from the current coroutine
 * if required.
 *
 * If QIO_CHANNEL_WRITE_FLAG_ZERO_COPY is passed in flags,
 * instead of waiting for all requested data to be written,
 * this function will wait until it's all queued for writing.
 * In this case, if the buffer gets changed between queueing and
 * sending, the updated buffer will be sent. If this is not a
 * desired behavior, it's suggested to call qio_channel_flush()
 * before reusing the buffer.
 *
 * Returns: 0 if all bytes were written, or -1 on error
 */

int qio_channel_writev_full_all(QIOChannel *ioc,
                                const struct iovec *iov,
                                size_t niov,
                                int *fds, size_t nfds,
                                int flags, Error **errp);

/**
 * qio_channel_flush:
 * @ioc: the channel object
 * @errp: pointer to a NULL-initialized error object
 *
 * Will block until every packet queued with
 * qio_channel_writev_full() + QIO_CHANNEL_WRITE_FLAG_ZERO_COPY
 * is sent, or return in case of any error.
 *
 * If not implemented, acts as a no-op, and returns 0.
 *
 * Returns -1 if any error is found,
 *          1 if every send failed to use zero copy.
 *          0 otherwise.
 */

int qio_channel_flush(QIOChannel *ioc,
                      Error **errp);

#endif /* QIO_CHANNEL_H */
