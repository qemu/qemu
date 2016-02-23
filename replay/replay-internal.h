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


enum ReplayEvents {
    /* for instruction event */
    EVENT_INSTRUCTION,
    /* for software interrupt */
    EVENT_INTERRUPT,
    /* for emulated exceptions */
    EVENT_EXCEPTION,
    /* for async events */
    EVENT_ASYNC,
    /* for shutdown request */
    EVENT_SHUTDOWN,
    /* for clock read/writes */
    /* some of greater codes are reserved for clocks */
    EVENT_CLOCK,
    EVENT_CLOCK_LAST = EVENT_CLOCK + REPLAY_CLOCK_COUNT - 1,
    /* for checkpoint event */
    /* some of greater codes are reserved for checkpoints */
    EVENT_CHECKPOINT,
    EVENT_CHECKPOINT_LAST = EVENT_CHECKPOINT + CHECKPOINT_COUNT - 1,
    /* end of log event */
    EVENT_END,
    EVENT_COUNT
};

/* Asynchronous events IDs */

enum ReplayAsyncEventKind {
    REPLAY_ASYNC_EVENT_BH,
    REPLAY_ASYNC_EVENT_INPUT,
    REPLAY_ASYNC_EVENT_INPUT_SYNC,
    REPLAY_ASYNC_COUNT
};

typedef enum ReplayAsyncEventKind ReplayAsyncEventKind;

typedef struct ReplayState {
    /*! Cached clock values. */
    int64_t cached_clock[REPLAY_CLOCK_COUNT];
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

/*! Reads next clock value from the file.
    If clock kind read from the file is different from the parameter,
    the value is not used. */
void replay_read_next_clock(unsigned int kind);

/* Asynchronous events queue */

/*! Initializes events' processing internals */
void replay_init_events(void);
/*! Clears internal data structures for events handling */
void replay_finish_events(void);
/*! Enables storing events in the queue */
void replay_enable_events(void);
/*! Flushes events queue */
void replay_flush_events(void);
/*! Clears events list before loading new VM state */
void replay_clear_events(void);
/*! Returns true if there are any unsaved events in the queue */
bool replay_has_events(void);
/*! Saves events from queue into the file */
void replay_save_events(int checkpoint);
/*! Read events from the file into the input queue */
void replay_read_events(int checkpoint);

/* Input events */

/*! Saves input event to the log */
void replay_save_input_event(InputEvent *evt);
/*! Reads input event from the log */
InputEvent *replay_read_input_event(void);
/*! Adds input event to the queue */
void replay_add_input_event(struct InputEvent *event);
/*! Adds input sync event to the queue */
void replay_add_input_sync_event(void);

#endif
