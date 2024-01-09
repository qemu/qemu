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

/* Asynchronous events IDs */

typedef enum ReplayAsyncEventKind {
    REPLAY_ASYNC_EVENT_BH,
    REPLAY_ASYNC_EVENT_BH_ONESHOT,
    REPLAY_ASYNC_EVENT_INPUT,
    REPLAY_ASYNC_EVENT_INPUT_SYNC,
    REPLAY_ASYNC_EVENT_CHAR_READ,
    REPLAY_ASYNC_EVENT_BLOCK,
    REPLAY_ASYNC_EVENT_NET,
    REPLAY_ASYNC_COUNT
} ReplayAsyncEventKind;

/*
 * Any changes to order/number of events will need to bump
 * REPLAY_VERSION to prevent confusion with old logs. Also don't
 * forget to update replay_event_name() to make your debugging life
 * easier.
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
    EVENT_ASYNC_LAST = EVENT_ASYNC + REPLAY_ASYNC_COUNT - 1,
    /* for shutdown requests, range allows recovery of ShutdownCause */
    EVENT_SHUTDOWN,
    EVENT_SHUTDOWN_LAST = EVENT_SHUTDOWN + SHUTDOWN_CAUSE__MAX,
    /* for character device write event */
    EVENT_CHAR_WRITE,
    /* for character device read all event */
    EVENT_CHAR_READ_ALL,
    EVENT_CHAR_READ_ALL_ERROR,
    /* for audio out event */
    EVENT_AUDIO_OUT,
    /* for audio in event */
    EVENT_AUDIO_IN,
    /* for random number generator */
    EVENT_RANDOM,
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

/**
 * typedef ReplayState - global tracking Replay state
 *
 * This structure tracks where we are in the current ReplayState
 * including the logged events from the recorded replay stream. Some
 * of the data is also stored/restored from VMStateDescription when VM
 * save/restore events take place.
 *
 * @cached_clock: Cached clocks values
 * @current_icount: number of processed instructions
 * @instruction_count: number of instructions until next event
 * @current_event: current event index
 * @data_kind: current event
 * @has_unread_data: true if event not yet processed
 * @file_offset: offset into replay log at replay snapshot
 * @block_request_id: current serialised block request id
 * @read_event_id: current async read event id
 */
typedef struct ReplayState {
    int64_t cached_clock[REPLAY_CLOCK_COUNT];
    uint64_t current_icount;
    int instruction_count;
    unsigned int current_event;
    unsigned int data_kind;
    bool has_unread_data;
    uint64_t file_offset;
    uint64_t block_request_id;
    uint64_t read_event_id;
} ReplayState;
extern ReplayState replay_state;

/* File for replay writing */
extern FILE *replay_file;
/* Instruction count of the replay breakpoint */
extern uint64_t replay_break_icount;
/* Timer for the replay breakpoint callback */
extern QEMUTimer *replay_break_timer;

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

/* Mutex functions for protecting replay log file and ensuring
 * synchronisation between vCPU and main-loop threads. */

void replay_mutex_init(void);
bool replay_mutex_locked(void);

/*! Checks error status of the file. */
void replay_check_error(void);

/*! Finishes processing of the replayed event and fetches
    the next event from the log. */
void replay_finish_event(void);
/*! Reads data type from the file and stores it in the
    data_kind variable. */
void replay_fetch_data_kind(void);

/*! Advance replay_state.current_icount to the specified value. */
void replay_advance_current_icount(uint64_t current_icount);
/*! Saves queued events (like instructions and sound). */
void replay_save_instructions(void);

/*! Skips async events until some sync event will be found.
    \return true, if event was found */
bool replay_next_event_is(int event);

/*! Reads next clock value from the file.
    If clock kind read from the file is different from the parameter,
    the value is not used. */
void replay_read_next_clock(ReplayClockKind kind);

/* Asynchronous events queue */

/*! Initializes events' processing internals */
void replay_init_events(void);
/*! Clears internal data structures for events handling */
void replay_finish_events(void);
/*! Returns true if there are any unsaved events in the queue */
bool replay_has_events(void);
/*! Saves events from queue into the file */
void replay_save_events(void);
/*! Read events from the file into the input queue */
void replay_read_events(void);
/*! Adds specified async event to the queue */
void replay_add_event(ReplayAsyncEventKind event_kind, void *opaque,
                      void *opaque2, uint64_t id);

/* Input events */

/*! Saves input event to the log */
void replay_save_input_event(InputEvent *evt);
/*! Reads input event from the log */
InputEvent *replay_read_input_event(void);
/*! Adds input event to the queue */
void replay_add_input_event(struct InputEvent *event);
/*! Adds input sync event to the queue */
void replay_add_input_sync_event(void);

/* Character devices */

/*! Called to run char device read event. */
void replay_event_char_read_run(void *opaque);
/*! Writes char read event to the file. */
void replay_event_char_read_save(void *opaque);
/*! Reads char event read from the file. */
void *replay_event_char_read_load(void);

/* Network devices */

/*! Called to run network event. */
void replay_event_net_run(void *opaque);
/*! Writes network event to the file. */
void replay_event_net_save(void *opaque);
/*! Reads network from the file. */
void *replay_event_net_load(void);

/* Diagnostics */

/**
 * replay_sync_error(): report sync error and exit
 *
 * When we reach an error condition we want to report it centrally so
 * we can also dump some useful information into the logs.
 */
G_NORETURN void replay_sync_error(const char *error);

/* VMState-related functions */

/* Registers replay VMState.
   Should be called before virtual devices initialization
   to make cached timers available for post_load functions. */
void replay_vmstate_register(void);

#endif
