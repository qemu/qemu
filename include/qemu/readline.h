#ifndef READLINE_H
#define READLINE_H

#define READLINE_CMD_BUF_SIZE 4095
#define READLINE_MAX_CMDS 64
#define READLINE_MAX_COMPLETIONS 256

typedef void G_GNUC_PRINTF(2, 3) ReadLinePrintfFunc(void *opaque,
                                                   const char *fmt, ...);
typedef void ReadLineFlushFunc(void *opaque);
typedef void ReadLineFunc(void *opaque, const char *str,
                          void *readline_opaque);
typedef void ReadLineCompletionFunc(void *opaque,
                                    const char *cmdline);

typedef struct ReadLineState {
    char cmd_buf[READLINE_CMD_BUF_SIZE + 1];
    int cmd_buf_index;
    int cmd_buf_size;

    char last_cmd_buf[READLINE_CMD_BUF_SIZE + 1];
    int last_cmd_buf_index;
    int last_cmd_buf_size;

    int esc_state;
    int esc_param;

    char *history[READLINE_MAX_CMDS];
    int hist_entry;

    ReadLineCompletionFunc *completion_finder;
    char *completions[READLINE_MAX_COMPLETIONS];
    int nb_completions;
    int completion_index;

    ReadLineFunc *readline_func;
    void *readline_opaque;
    int read_password;
    char prompt[256];

    ReadLinePrintfFunc *printf_func;
    ReadLineFlushFunc *flush_func;
    void *opaque;
} ReadLineState;

void readline_add_completion(ReadLineState *rs, const char *str);
void readline_add_completion_of(ReadLineState *rs,
                                const char *pfx, const char *str);
void readline_set_completion_index(ReadLineState *rs, int completion_index);

const char *readline_get_history(ReadLineState *rs, unsigned int index);

void readline_handle_byte(ReadLineState *rs, int ch);

void readline_start(ReadLineState *rs, const char *prompt, int read_password,
                    ReadLineFunc *readline_func, void *readline_opaque);
void readline_restart(ReadLineState *rs);
void readline_show_prompt(ReadLineState *rs);

ReadLineState *readline_init(ReadLinePrintfFunc *printf_func,
                             ReadLineFlushFunc *flush_func,
                             void *opaque,
                             ReadLineCompletionFunc *completion_finder);
void readline_free(ReadLineState *rs);

#endif /* READLINE_H */
