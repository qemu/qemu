#ifndef REPLAY_INTERNAL_H
#define REPLAY_INTERNAL_H

/*
 * replay-internal.h
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <stdio.h>

enum ReplayEvents {
    /* for instruction event */
    EVENT_INSTRUCTION,
    EVENT_COUNT
};

typedef struct ReplayState {
    /*! Current step - number of processed instructions and timer events. */
    uint64_t current_step;
    /*! Number of instructions to be executed before other events happen. */
    int instructions_count;
} ReplayState;
extern ReplayState replay_state;

extern unsigned int replay_data_kind;

/* File for replay writing */
extern FILE *replay_file;

void replay_put_byte(uint8_t byte);
void replay_put_event(uint8_t event);
void replay_put_word(uint16_t word);
void replay_put_dword(uint32_t dword);
void replay_put_qword(int64_t qword);
void replay_put_array(const uint8_t *buf, size_t size);

uint8_t replay_get_byte(void);
uint16_t replay_get_word(void);
uint32_t replay_get_dword(void);
int64_t replay_get_qword(void);
void replay_get_array(uint8_t *buf, size_t *size);
void replay_get_array_alloc(uint8_t **buf, size_t *size);

/* Mutex functions for protecting replay log file */

void replay_mutex_init(void);
void replay_mutex_destroy(void);
void replay_mutex_lock(void);
void replay_mutex_unlock(void);

/*! Checks error status of the file. */
void replay_check_error(void);

/*! Finishes processing of the replayed event and fetches
    the next event from the log. */
void replay_finish_event(void);
/*! Reads data type from the file and stores it in the
    replay_data_kind variable. */
void replay_fetch_data_kind(void);

/*! Saves queued events (like instructions and sound). */
void replay_save_instructions(void);

/*! Skips async events until some sync event will be found.
    \return true, if event was found */
bool replay_next_event_is(int event);

#endif
