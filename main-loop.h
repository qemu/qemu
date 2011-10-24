/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_MAIN_LOOP_H
#define QEMU_MAIN_LOOP_H 1

#ifdef SIGRTMIN
#define SIG_IPI (SIGRTMIN+4)
#else
#define SIG_IPI SIGUSR1
#endif

/**
 * qemu_init_main_loop: Set up the process so that it can run the main loop.
 *
 * This includes setting up signal handlers.  It should be called before
 * any other threads are created.  In addition, threads other than the
 * main one should block signals that are trapped by the main loop.
 * For simplicity, you can consider these signals to be safe: SIGUSR1,
 * SIGUSR2, thread signals (SIGFPE, SIGILL, SIGSEGV, SIGBUS) and real-time
 * signals if available.  Remember that Windows in practice does not have
 * signals, though.
 */
int qemu_init_main_loop(void);

/**
 * main_loop_wait: Run one iteration of the main loop.
 *
 * If @nonblocking is true, poll for events, otherwise suspend until
 * one actually occurs.  The main loop usually consists of a loop that
 * repeatedly calls main_loop_wait(false).
 *
 * Main loop services include file descriptor callbacks, bottom halves
 * and timers (defined in qemu-timer.h).  Bottom halves are similar to timers
 * that execute immediately, but have a lower overhead and scheduling them
 * is wait-free, thread-safe and signal-safe.
 *
 * It is sometimes useful to put a whole program in a coroutine.  In this
 * case, the coroutine actually should be started from within the main loop,
 * so that the main loop can run whenever the coroutine yields.  To do this,
 * you can use a bottom half to enter the coroutine as soon as the main loop
 * starts:
 *
 *     void enter_co_bh(void *opaque) {
 *         QEMUCoroutine *co = opaque;
 *         qemu_coroutine_enter(co, NULL);
 *     }
 *
 *     ...
 *     QEMUCoroutine *co = qemu_coroutine_create(coroutine_entry);
 *     QEMUBH *start_bh = qemu_bh_new(enter_co_bh, co);
 *     qemu_bh_schedule(start_bh);
 *     while (...) {
 *         main_loop_wait(false);
 *     }
 *
 * (In the future we may provide a wrapper for this).
 *
 * @nonblocking: Whether the caller should block until an event occurs.
 */
int main_loop_wait(int nonblocking);

/**
 * qemu_notify_event: Force processing of pending events.
 *
 * Similar to signaling a condition variable, qemu_notify_event forces
 * main_loop_wait to look at pending events and exit.  The caller of
 * main_loop_wait will usually call it again very soon, so qemu_notify_event
 * also has the side effect of recalculating the sets of file descriptors
 * that the main loop waits for.
 *
 * Calling qemu_notify_event is rarely necessary, because main loop
 * services (bottom halves and timers) call it themselves.  One notable
 * exception occurs when using qemu_set_fd_handler2 (see below).
 */
void qemu_notify_event(void);

#ifdef _WIN32
/* return TRUE if no sleep should be done afterwards */
typedef int PollingFunc(void *opaque);

/**
 * qemu_add_polling_cb: Register a Windows-specific polling callback
 *
 * Currently, under Windows some events are polled rather than waited for.
 * Polling callbacks do not ensure that @func is called timely, because
 * the main loop might wait for an arbitrarily long time.  If possible,
 * you should instead create a separate thread that does a blocking poll
 * and set a Win32 event object.  The event can then be passed to
 * qemu_add_wait_object.
 *
 * Polling callbacks really have nothing Windows specific in them, but
 * as they are a hack and are currenly not necessary under POSIX systems,
 * they are only available when QEMU is running under Windows.
 *
 * @func: The function that does the polling, and returns 1 to force
 * immediate completion of main_loop_wait.
 * @opaque: A pointer-size value that is passed to @func.
 */
int qemu_add_polling_cb(PollingFunc *func, void *opaque);

/**
 * qemu_del_polling_cb: Unregister a Windows-specific polling callback
 *
 * This function removes a callback that was registered with
 * qemu_add_polling_cb.
 *
 * @func: The function that was passed to qemu_add_polling_cb.
 * @opaque: A pointer-size value that was passed to qemu_add_polling_cb.
 */
void qemu_del_polling_cb(PollingFunc *func, void *opaque);

/* Wait objects handling */
typedef void WaitObjectFunc(void *opaque);

/**
 * qemu_add_wait_object: Register a callback for a Windows handle
 *
 * Under Windows, the iohandler mechanism can only be used with sockets.
 * QEMU must use the WaitForMultipleObjects API to wait on other handles.
 * This function registers a #HANDLE with QEMU, so that it will be included
 * in the main loop's calls to WaitForMultipleObjects.  When the handle
 * is in a signaled state, QEMU will call @func.
 *
 * @handle: The Windows handle to be observed.
 * @func: A function to be called when @handle is in a signaled state.
 * @opaque: A pointer-size value that is passed to @func.
 */
int qemu_add_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque);

/**
 * qemu_del_wait_object: Unregister a callback for a Windows handle
 *
 * This function removes a callback that was registered with
 * qemu_add_wait_object.
 *
 * @func: The function that was passed to qemu_add_wait_object.
 * @opaque: A pointer-size value that was passed to qemu_add_wait_object.
 */
void qemu_del_wait_object(HANDLE handle, WaitObjectFunc *func, void *opaque);
#endif

/* async I/O support */

typedef void IOReadHandler(void *opaque, const uint8_t *buf, int size);
typedef int IOCanReadHandler(void *opaque);
typedef void IOHandler(void *opaque);

/**
 * qemu_set_fd_handler2: Register a file descriptor with the main loop
 *
 * This function tells the main loop to wake up whenever one of the
 * following conditions is true:
 *
 * 1) if @fd_write is not %NULL, when the file descriptor is writable;
 *
 * 2) if @fd_read is not %NULL, when the file descriptor is readable.
 *
 * @fd_read_poll can be used to disable the @fd_read callback temporarily.
 * This is useful to avoid calling qemu_set_fd_handler2 every time the
 * client becomes interested in reading (or dually, stops being interested).
 * A typical example is when @fd is a listening socket and you want to bound
 * the number of active clients.  Remember to call qemu_notify_event whenever
 * the condition may change from %false to %true.
 *
 * The callbacks that are set up by qemu_set_fd_handler2 are level-triggered.
 * If @fd_read does not read from @fd, or @fd_write does not write to @fd
 * until its buffers are full, they will be called again on the next
 * iteration.
 *
 * @fd: The file descriptor to be observed.  Under Windows it must be
 * a #SOCKET.
 *
 * @fd_read_poll: A function that returns 1 if the @fd_read callback
 * should be fired.  If the function returns 0, the main loop will not
 * end its iteration even if @fd becomes readable.
 *
 * @fd_read: A level-triggered callback that is fired if @fd is readable
 * at the beginning of a main loop iteration, or if it becomes readable
 * during one.
 *
 * @fd_write: A level-triggered callback that is fired when @fd is writable
 * at the beginning of a main loop iteration, or if it becomes writable
 * during one.
 *
 * @opaque: A pointer-sized value that is passed to @fd_read_poll,
 * @fd_read and @fd_write.
 */
int qemu_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque);

/**
 * qemu_set_fd_handler: Register a file descriptor with the main loop
 *
 * This function tells the main loop to wake up whenever one of the
 * following conditions is true:
 *
 * 1) if @fd_write is not %NULL, when the file descriptor is writable;
 *
 * 2) if @fd_read is not %NULL, when the file descriptor is readable.
 *
 * The callbacks that are set up by qemu_set_fd_handler are level-triggered.
 * If @fd_read does not read from @fd, or @fd_write does not write to @fd
 * until its buffers are full, they will be called again on the next
 * iteration.
 *
 * @fd: The file descriptor to be observed.  Under Windows it must be
 * a #SOCKET.
 *
 * @fd_read: A level-triggered callback that is fired if @fd is readable
 * at the beginning of a main loop iteration, or if it becomes readable
 * during one.
 *
 * @fd_write: A level-triggered callback that is fired when @fd is writable
 * at the beginning of a main loop iteration, or if it becomes writable
 * during one.
 *
 * @opaque: A pointer-sized value that is passed to @fd_read and @fd_write.
 */
int qemu_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque);

typedef struct QEMUBH QEMUBH;
typedef void QEMUBHFunc(void *opaque);

/**
 * qemu_bh_new: Allocate a new bottom half structure.
 *
 * Bottom halves are lightweight callbacks whose invocation is guaranteed
 * to be wait-free, thread-safe and signal-safe.  The #QEMUBH structure
 * is opaque and must be allocated prior to its use.
 */
QEMUBH *qemu_bh_new(QEMUBHFunc *cb, void *opaque);

/**
 * qemu_bh_schedule: Schedule a bottom half.
 *
 * Scheduling a bottom half interrupts the main loop and causes the
 * execution of the callback that was passed to qemu_bh_new.
 *
 * Bottom halves that are scheduled from a bottom half handler are instantly
 * invoked.  This can create an infinite loop if a bottom half handler
 * schedules itself.
 *
 * @bh: The bottom half to be scheduled.
 */
void qemu_bh_schedule(QEMUBH *bh);

/**
 * qemu_bh_cancel: Cancel execution of a bottom half.
 *
 * Canceling execution of a bottom half undoes the effect of calls to
 * qemu_bh_schedule without freeing its resources yet.  While cancellation
 * itself is also wait-free and thread-safe, it can of course race with the
 * loop that executes bottom halves unless you are holding the iothread
 * mutex.  This makes it mostly useless if you are not holding the mutex.
 *
 * @bh: The bottom half to be canceled.
 */
void qemu_bh_cancel(QEMUBH *bh);

/**
 *qemu_bh_delete: Cancel execution of a bottom half and free its resources.
 *
 * Deleting a bottom half frees the memory that was allocated for it by
 * qemu_bh_new.  It also implies canceling the bottom half if it was
 * scheduled.
 *
 * @bh: The bottom half to be deleted.
 */
void qemu_bh_delete(QEMUBH *bh);

#ifdef CONFIG_POSIX
/**
 * qemu_add_child_watch: Register a child process for reaping.
 *
 * Under POSIX systems, a parent process must read the exit status of
 * its child processes using waitpid, or the operating system will not
 * free some of the resources attached to that process.
 *
 * This function directs the QEMU main loop to observe a child process
 * and call waitpid as soon as it exits; the watch is then removed
 * automatically.  It is useful whenever QEMU forks a child process
 * but will find out about its termination by other means such as a
 * "broken pipe".
 *
 * @pid: The pid that QEMU should observe.
 */
int qemu_add_child_watch(pid_t pid);
#endif

/**
 * qemu_mutex_lock_iothread: Lock the main loop mutex.
 *
 * This function locks the main loop mutex.  The mutex is taken by
 * qemu_init_main_loop and always taken except while waiting on
 * external events (such as with select).  The mutex should be taken
 * by threads other than the main loop thread when calling
 * qemu_bh_new(), qemu_set_fd_handler() and basically all other
 * functions documented in this file.
 */
void qemu_mutex_lock_iothread(void);

/**
 * qemu_mutex_unlock_iothread: Unlock the main loop mutex.
 *
 * This function unlocks the main loop mutex.  The mutex is taken by
 * qemu_init_main_loop and always taken except while waiting on
 * external events (such as with select).  The mutex should be unlocked
 * as soon as possible by threads other than the main loop thread,
 * because it prevents the main loop from processing callbacks,
 * including timers and bottom halves.
 */
void qemu_mutex_unlock_iothread(void);

/* internal interfaces */

void qemu_iohandler_fill(int *pnfds, fd_set *readfds, fd_set *writefds, fd_set *xfds);
void qemu_iohandler_poll(fd_set *readfds, fd_set *writefds, fd_set *xfds, int rc);

void qemu_bh_schedule_idle(QEMUBH *bh);
int qemu_bh_poll(void);
void qemu_bh_update_timeout(int *timeout);

#endif
