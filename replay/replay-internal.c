/*
 * replay-internal.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/replay.h"
#include "system/runstate.h"
#include "replay-internal.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

/* Mutex to protect reading and writing events to the log.
   data_kind and has_unread_data are also protected
   by this mutex.
   It also protects replay events queue which stores events to be
   written or read to the log. */
static QemuMutex lock;
/* Condition and queue for fair ordering of mutex lock requests. */
static QemuCond mutex_cond;
static unsigned long mutex_head, mutex_tail;

/* File for replay writing */
static bool write_error;
FILE *replay_file;

static void replay_write_error(void)
{
    if (!write_error) {
        error_report("replay write error");
        write_error = true;
    }
}

static void replay_read_error(void)
{
    error_report("error reading the replay data");
    exit(1);
}

void replay_put_byte(uint8_t byte)
{
    if (replay_file) {
        if (putc(byte, replay_file) == EOF) {
            replay_write_error();
        }
    }
}

void replay_put_event(uint8_t event)
{
    assert(event < EVENT_COUNT);
    replay_put_byte(event);
}


void replay_put_word(uint16_t word)
{
    replay_put_byte(word >> 8);
    replay_put_byte(word);
}

void replay_put_dword(uint32_t dword)
{
    replay_put_word(dword >> 16);
    replay_put_word(dword);
}

void replay_put_qword(int64_t qword)
{
    replay_put_dword(qword >> 32);
    replay_put_dword(qword);
}

void replay_put_array(const uint8_t *buf, size_t size)
{
    if (replay_file) {
        replay_put_dword(size);
        if (fwrite(buf, 1, size, replay_file) != size) {
            replay_write_error();
        }
    }
}

uint8_t replay_get_byte(void)
{
    uint8_t byte = 0;
    if (replay_file) {
        int r = getc(replay_file);
        if (r == EOF) {
            replay_read_error();
        }
        byte = r;
    }
    return byte;
}

uint16_t replay_get_word(void)
{
    uint16_t word = 0;
    if (replay_file) {
        word = replay_get_byte();
        word = (word << 8) + replay_get_byte();
    }

    return word;
}

uint32_t replay_get_dword(void)
{
    uint32_t dword = 0;
    if (replay_file) {
        dword = replay_get_word();
        dword = (dword << 16) + replay_get_word();
    }

    return dword;
}

int64_t replay_get_qword(void)
{
    int64_t qword = 0;
    if (replay_file) {
        qword = replay_get_dword();
        qword = (qword << 32) + replay_get_dword();
    }

    return qword;
}

void replay_get_array(uint8_t *buf, size_t *size)
{
    if (replay_file) {
        *size = replay_get_dword();
        if (fread(buf, 1, *size, replay_file) != *size) {
            replay_read_error();
        }
    }
}

void replay_get_array_alloc(uint8_t **buf, size_t *size)
{
    if (replay_file) {
        *size = replay_get_dword();
        *buf = g_malloc(*size);
        if (fread(*buf, 1, *size, replay_file) != *size) {
            replay_read_error();
        }
    }
}

void replay_check_error(void)
{
    if (replay_file) {
        if (feof(replay_file)) {
            error_report("replay file is over");
            qemu_system_vmstop_request_prepare();
            qemu_system_vmstop_request(RUN_STATE_PAUSED);
        } else if (ferror(replay_file)) {
            error_report("replay file is over or something goes wrong");
            qemu_system_vmstop_request_prepare();
            qemu_system_vmstop_request(RUN_STATE_INTERNAL_ERROR);
        }
    }
}

void replay_fetch_data_kind(void)
{
    if (replay_file) {
        if (!replay_state.has_unread_data) {
            replay_state.data_kind = replay_get_byte();
            replay_state.current_event++;
            if (replay_state.data_kind == EVENT_INSTRUCTION) {
                replay_state.instruction_count = replay_get_dword();
            }
            replay_check_error();
            replay_state.has_unread_data = true;
            if (replay_state.data_kind >= EVENT_COUNT) {
                error_report("Replay: unknown event kind %d",
                             replay_state.data_kind);
                exit(1);
            }
        }
    }
}

void replay_finish_event(void)
{
    replay_state.has_unread_data = false;
    replay_fetch_data_kind();
}

static __thread bool replay_locked;

void replay_mutex_init(void)
{
    qemu_mutex_init(&lock);
    qemu_cond_init(&mutex_cond);
    /* Hold the mutex while we start-up */
    replay_locked = true;
    ++mutex_tail;
}

bool replay_mutex_locked(void)
{
    return replay_locked;
}

/* Ordering constraints, replay_lock must be taken before BQL */
void replay_mutex_lock(void)
{
    if (replay_mode != REPLAY_MODE_NONE) {
        unsigned long id;
        g_assert(!bql_locked());
        g_assert(!replay_mutex_locked());
        qemu_mutex_lock(&lock);
        id = mutex_tail++;
        while (id != mutex_head) {
            qemu_cond_wait(&mutex_cond, &lock);
        }
        replay_locked = true;
        qemu_mutex_unlock(&lock);
    }
}

void replay_mutex_unlock(void)
{
    if (replay_mode != REPLAY_MODE_NONE) {
        g_assert(replay_mutex_locked());
        qemu_mutex_lock(&lock);
        ++mutex_head;
        replay_locked = false;
        qemu_cond_broadcast(&mutex_cond);
        qemu_mutex_unlock(&lock);
    }
}

void replay_advance_current_icount(uint64_t current_icount)
{
    int diff = (int)(current_icount - replay_state.current_icount);

    /* Time can only go forward */
    assert(diff >= 0);

    if (replay_mode == REPLAY_MODE_RECORD) {
        if (diff > 0) {
            replay_put_event(EVENT_INSTRUCTION);
            replay_put_dword(diff);
            replay_state.current_icount += diff;
        }
    } else if (replay_mode == REPLAY_MODE_PLAY) {
        if (diff > 0) {
            replay_state.instruction_count -= diff;
            replay_state.current_icount += diff;
            if (replay_state.instruction_count == 0) {
                assert(replay_state.data_kind == EVENT_INSTRUCTION);
                replay_finish_event();
                /* Wake up iothread. This is required because
                    timers will not expire until clock counters
                    will be read from the log. */
                qemu_notify_event();
            }
        }
        /* Execution reached the break step */
        if (replay_break_icount == replay_state.current_icount) {
            /* Cannot make callback directly from the vCPU thread */
            timer_mod_ns(replay_break_timer,
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME));
        }
    }
}

/*! Saves cached instructions. */
void replay_save_instructions(void)
{
    if (replay_file && replay_mode == REPLAY_MODE_RECORD) {
        g_assert(replay_mutex_locked());
        replay_advance_current_icount(replay_get_current_icount());
    }
}
