/*
 * QEMU readline utility
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "qemu-common.h"
#include "console.h"

#define TERM_CMD_BUF_SIZE 4095
#define TERM_MAX_CMDS 64
#define NB_COMPLETIONS_MAX 256

#define IS_NORM 0
#define IS_ESC  1
#define IS_CSI  2

#define printf do_not_use_printf

static char term_cmd_buf[TERM_CMD_BUF_SIZE + 1];
static int term_cmd_buf_index;
static int term_cmd_buf_size;

static char term_last_cmd_buf[TERM_CMD_BUF_SIZE + 1];
static int term_last_cmd_buf_index;
static int term_last_cmd_buf_size;

static int term_esc_state;
static int term_esc_param;

static char *term_history[TERM_MAX_CMDS];
static int term_hist_entry = -1;

static int nb_completions;
int completion_index;
static char *completions[NB_COMPLETIONS_MAX];

static ReadLineFunc *term_readline_func;
static int term_is_password;
static char term_prompt[256];
static void *term_readline_opaque;

static void term_show_prompt2(void)
{
    term_printf("%s", term_prompt);
    term_flush();
    term_last_cmd_buf_index = 0;
    term_last_cmd_buf_size = 0;
    term_esc_state = IS_NORM;
}

static void term_show_prompt(void)
{
    term_show_prompt2();
    term_cmd_buf_index = 0;
    term_cmd_buf_size = 0;
}

/* update the displayed command line */
static void term_update(void)
{
    int i, delta, len;

    if (term_cmd_buf_size != term_last_cmd_buf_size ||
        memcmp(term_cmd_buf, term_last_cmd_buf, term_cmd_buf_size) != 0) {
        for(i = 0; i < term_last_cmd_buf_index; i++) {
            term_printf("\033[D");
        }
        term_cmd_buf[term_cmd_buf_size] = '\0';
        if (term_is_password) {
            len = strlen(term_cmd_buf);
            for(i = 0; i < len; i++)
                term_printf("*");
        } else {
            term_printf("%s", term_cmd_buf);
        }
        term_printf("\033[K");
        memcpy(term_last_cmd_buf, term_cmd_buf, term_cmd_buf_size);
        term_last_cmd_buf_size = term_cmd_buf_size;
        term_last_cmd_buf_index = term_cmd_buf_size;
    }
    if (term_cmd_buf_index != term_last_cmd_buf_index) {
        delta = term_cmd_buf_index - term_last_cmd_buf_index;
        if (delta > 0) {
            for(i = 0;i < delta; i++) {
                term_printf("\033[C");
            }
        } else {
            delta = -delta;
            for(i = 0;i < delta; i++) {
                term_printf("\033[D");
            }
        }
        term_last_cmd_buf_index = term_cmd_buf_index;
    }
    term_flush();
}

static void term_insert_char(int ch)
{
    if (term_cmd_buf_index < TERM_CMD_BUF_SIZE) {
        memmove(term_cmd_buf + term_cmd_buf_index + 1,
                term_cmd_buf + term_cmd_buf_index,
                term_cmd_buf_size - term_cmd_buf_index);
        term_cmd_buf[term_cmd_buf_index] = ch;
        term_cmd_buf_size++;
        term_cmd_buf_index++;
    }
}

static void term_backward_char(void)
{
    if (term_cmd_buf_index > 0) {
        term_cmd_buf_index--;
    }
}

static void term_forward_char(void)
{
    if (term_cmd_buf_index < term_cmd_buf_size) {
        term_cmd_buf_index++;
    }
}

static void term_delete_char(void)
{
    if (term_cmd_buf_index < term_cmd_buf_size) {
        memmove(term_cmd_buf + term_cmd_buf_index,
                term_cmd_buf + term_cmd_buf_index + 1,
                term_cmd_buf_size - term_cmd_buf_index - 1);
        term_cmd_buf_size--;
    }
}

static void term_backspace(void)
{
    if (term_cmd_buf_index > 0) {
        term_backward_char();
        term_delete_char();
    }
}

static void term_backword(void)
{
    int start;

    if (term_cmd_buf_index == 0 || term_cmd_buf_index > term_cmd_buf_size) {
        return;
    }

    start = term_cmd_buf_index - 1;

    /* find first word (backwards) */
    while (start > 0) {
        if (!qemu_isspace(term_cmd_buf[start])) {
            break;
        }

        --start;
    }

    /* find first space (backwards) */
    while (start > 0) {
        if (qemu_isspace(term_cmd_buf[start])) {
            ++start;
            break;
        }

        --start;
    }

    /* remove word */
    if (start < term_cmd_buf_index) {
        memmove(term_cmd_buf + start,
                term_cmd_buf + term_cmd_buf_index,
                term_cmd_buf_size - term_cmd_buf_index);
        term_cmd_buf_size -= term_cmd_buf_index - start;
        term_cmd_buf_index = start;
    }
}

static void term_bol(void)
{
    term_cmd_buf_index = 0;
}

static void term_eol(void)
{
    term_cmd_buf_index = term_cmd_buf_size;
}

static void term_up_char(void)
{
    int idx;

    if (term_hist_entry == 0)
	return;
    if (term_hist_entry == -1) {
	/* Find latest entry */
	for (idx = 0; idx < TERM_MAX_CMDS; idx++) {
	    if (term_history[idx] == NULL)
		break;
	}
	term_hist_entry = idx;
    }
    term_hist_entry--;
    if (term_hist_entry >= 0) {
	pstrcpy(term_cmd_buf, sizeof(term_cmd_buf),
                term_history[term_hist_entry]);
	term_cmd_buf_index = term_cmd_buf_size = strlen(term_cmd_buf);
    }
}

static void term_down_char(void)
{
    if (term_hist_entry == TERM_MAX_CMDS - 1 || term_hist_entry == -1)
	return;
    if (term_history[++term_hist_entry] != NULL) {
	pstrcpy(term_cmd_buf, sizeof(term_cmd_buf),
                term_history[term_hist_entry]);
    } else {
	term_hist_entry = -1;
    }
    term_cmd_buf_index = term_cmd_buf_size = strlen(term_cmd_buf);
}

static void term_hist_add(const char *cmdline)
{
    char *hist_entry, *new_entry;
    int idx;

    if (cmdline[0] == '\0')
	return;
    new_entry = NULL;
    if (term_hist_entry != -1) {
	/* We were editing an existing history entry: replace it */
	hist_entry = term_history[term_hist_entry];
	idx = term_hist_entry;
	if (strcmp(hist_entry, cmdline) == 0) {
	    goto same_entry;
	}
    }
    /* Search cmdline in history buffers */
    for (idx = 0; idx < TERM_MAX_CMDS; idx++) {
	hist_entry = term_history[idx];
	if (hist_entry == NULL)
	    break;
	if (strcmp(hist_entry, cmdline) == 0) {
	same_entry:
	    new_entry = hist_entry;
	    /* Put this entry at the end of history */
	    memmove(&term_history[idx], &term_history[idx + 1],
		    (TERM_MAX_CMDS - idx + 1) * sizeof(char *));
	    term_history[TERM_MAX_CMDS - 1] = NULL;
	    for (; idx < TERM_MAX_CMDS; idx++) {
		if (term_history[idx] == NULL)
		    break;
	    }
	    break;
	}
    }
    if (idx == TERM_MAX_CMDS) {
	/* Need to get one free slot */
	free(term_history[0]);
	memcpy(term_history, &term_history[1],
	       (TERM_MAX_CMDS - 1) * sizeof(char *));
	term_history[TERM_MAX_CMDS - 1] = NULL;
	idx = TERM_MAX_CMDS - 1;
    }
    if (new_entry == NULL)
	new_entry = strdup(cmdline);
    term_history[idx] = new_entry;
    term_hist_entry = -1;
}

/* completion support */

void add_completion(const char *str)
{
    if (nb_completions < NB_COMPLETIONS_MAX) {
        completions[nb_completions++] = qemu_strdup(str);
    }
}

static void term_completion(void)
{
    int len, i, j, max_width, nb_cols, max_prefix;
    char *cmdline;

    nb_completions = 0;

    cmdline = qemu_malloc(term_cmd_buf_index + 1);
    if (!cmdline)
        return;
    memcpy(cmdline, term_cmd_buf, term_cmd_buf_index);
    cmdline[term_cmd_buf_index] = '\0';
    readline_find_completion(cmdline);
    qemu_free(cmdline);

    /* no completion found */
    if (nb_completions <= 0)
        return;
    if (nb_completions == 1) {
        len = strlen(completions[0]);
        for(i = completion_index; i < len; i++) {
            term_insert_char(completions[0][i]);
        }
        /* extra space for next argument. XXX: make it more generic */
        if (len > 0 && completions[0][len - 1] != '/')
            term_insert_char(' ');
    } else {
        term_printf("\n");
        max_width = 0;
        max_prefix = 0;	
        for(i = 0; i < nb_completions; i++) {
            len = strlen(completions[i]);
            if (i==0) {
                max_prefix = len;
            } else {
                if (len < max_prefix)
                    max_prefix = len;
                for(j=0; j<max_prefix; j++) {
                    if (completions[i][j] != completions[0][j])
                        max_prefix = j;
                }
            }
            if (len > max_width)
                max_width = len;
        }
        if (max_prefix > 0) 
            for(i = completion_index; i < max_prefix; i++) {
                term_insert_char(completions[0][i]);
            }
        max_width += 2;
        if (max_width < 10)
            max_width = 10;
        else if (max_width > 80)
            max_width = 80;
        nb_cols = 80 / max_width;
        j = 0;
        for(i = 0; i < nb_completions; i++) {
            term_printf("%-*s", max_width, completions[i]);
            if (++j == nb_cols || i == (nb_completions - 1)) {
                term_printf("\n");
                j = 0;
            }
        }
        term_show_prompt2();
    }
}

/* return true if command handled */
void readline_handle_byte(int ch)
{
    switch(term_esc_state) {
    case IS_NORM:
        switch(ch) {
        case 1:
            term_bol();
            break;
        case 4:
            term_delete_char();
            break;
        case 5:
            term_eol();
            break;
        case 9:
            term_completion();
            break;
        case 10:
        case 13:
            term_cmd_buf[term_cmd_buf_size] = '\0';
            if (!term_is_password)
                term_hist_add(term_cmd_buf);
            term_printf("\n");
            term_cmd_buf_index = 0;
            term_cmd_buf_size = 0;
            term_last_cmd_buf_index = 0;
            term_last_cmd_buf_size = 0;
            /* NOTE: readline_start can be called here */
            term_readline_func(term_readline_opaque, term_cmd_buf);
            break;
        case 23:
            /* ^W */
            term_backword();
            break;
        case 27:
            term_esc_state = IS_ESC;
            break;
        case 127:
        case 8:
            term_backspace();
            break;
	case 155:
            term_esc_state = IS_CSI;
	    break;
        default:
            if (ch >= 32) {
                term_insert_char(ch);
            }
            break;
        }
        break;
    case IS_ESC:
        if (ch == '[') {
            term_esc_state = IS_CSI;
            term_esc_param = 0;
        } else {
            term_esc_state = IS_NORM;
        }
        break;
    case IS_CSI:
        switch(ch) {
	case 'A':
	case 'F':
	    term_up_char();
	    break;
	case 'B':
	case 'E':
	    term_down_char();
	    break;
        case 'D':
            term_backward_char();
            break;
        case 'C':
            term_forward_char();
            break;
        case '0' ... '9':
            term_esc_param = term_esc_param * 10 + (ch - '0');
            goto the_end;
        case '~':
            switch(term_esc_param) {
            case 1:
                term_bol();
                break;
            case 3:
                term_delete_char();
                break;
            case 4:
                term_eol();
                break;
            }
            break;
        default:
            break;
        }
        term_esc_state = IS_NORM;
    the_end:
        break;
    }
    term_update();
}

void readline_start(const char *prompt, int is_password,
                    ReadLineFunc *readline_func, void *opaque)
{
    pstrcpy(term_prompt, sizeof(term_prompt), prompt);
    term_readline_func = readline_func;
    term_readline_opaque = opaque;
    term_is_password = is_password;
    term_show_prompt();
}

const char *readline_get_history(unsigned int index)
{
    if (index >= TERM_MAX_CMDS)
        return NULL;
    return term_history[index];
}
