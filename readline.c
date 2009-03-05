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
#include "readline.h"
#include "monitor.h"

#define IS_NORM 0
#define IS_ESC  1
#define IS_CSI  2

#define printf do_not_use_printf

void readline_show_prompt(ReadLineState *rs)
{
    monitor_printf(rs->mon, "%s", rs->prompt);
    monitor_flush(rs->mon);
    rs->last_cmd_buf_index = 0;
    rs->last_cmd_buf_size = 0;
    rs->esc_state = IS_NORM;
}

/* update the displayed command line */
static void readline_update(ReadLineState *rs)
{
    int i, delta, len;

    if (rs->cmd_buf_size != rs->last_cmd_buf_size ||
        memcmp(rs->cmd_buf, rs->last_cmd_buf, rs->cmd_buf_size) != 0) {
        for(i = 0; i < rs->last_cmd_buf_index; i++) {
            monitor_printf(rs->mon, "\033[D");
        }
        rs->cmd_buf[rs->cmd_buf_size] = '\0';
        if (rs->read_password) {
            len = strlen(rs->cmd_buf);
            for(i = 0; i < len; i++)
                monitor_printf(rs->mon, "*");
        } else {
            monitor_printf(rs->mon, "%s", rs->cmd_buf);
        }
        monitor_printf(rs->mon, "\033[K");
        memcpy(rs->last_cmd_buf, rs->cmd_buf, rs->cmd_buf_size);
        rs->last_cmd_buf_size = rs->cmd_buf_size;
        rs->last_cmd_buf_index = rs->cmd_buf_size;
    }
    if (rs->cmd_buf_index != rs->last_cmd_buf_index) {
        delta = rs->cmd_buf_index - rs->last_cmd_buf_index;
        if (delta > 0) {
            for(i = 0;i < delta; i++) {
                monitor_printf(rs->mon, "\033[C");
            }
        } else {
            delta = -delta;
            for(i = 0;i < delta; i++) {
                monitor_printf(rs->mon, "\033[D");
            }
        }
        rs->last_cmd_buf_index = rs->cmd_buf_index;
    }
    monitor_flush(rs->mon);
}

static void readline_insert_char(ReadLineState *rs, int ch)
{
    if (rs->cmd_buf_index < READLINE_CMD_BUF_SIZE) {
        memmove(rs->cmd_buf + rs->cmd_buf_index + 1,
                rs->cmd_buf + rs->cmd_buf_index,
                rs->cmd_buf_size - rs->cmd_buf_index);
        rs->cmd_buf[rs->cmd_buf_index] = ch;
        rs->cmd_buf_size++;
        rs->cmd_buf_index++;
    }
}

static void readline_backward_char(ReadLineState *rs)
{
    if (rs->cmd_buf_index > 0) {
        rs->cmd_buf_index--;
    }
}

static void readline_forward_char(ReadLineState *rs)
{
    if (rs->cmd_buf_index < rs->cmd_buf_size) {
        rs->cmd_buf_index++;
    }
}

static void readline_delete_char(ReadLineState *rs)
{
    if (rs->cmd_buf_index < rs->cmd_buf_size) {
        memmove(rs->cmd_buf + rs->cmd_buf_index,
                rs->cmd_buf + rs->cmd_buf_index + 1,
                rs->cmd_buf_size - rs->cmd_buf_index - 1);
        rs->cmd_buf_size--;
    }
}

static void readline_backspace(ReadLineState *rs)
{
    if (rs->cmd_buf_index > 0) {
        readline_backward_char(rs);
        readline_delete_char(rs);
    }
}

static void readline_backword(ReadLineState *rs)
{
    int start;

    if (rs->cmd_buf_index == 0 || rs->cmd_buf_index > rs->cmd_buf_size) {
        return;
    }

    start = rs->cmd_buf_index - 1;

    /* find first word (backwards) */
    while (start > 0) {
        if (!qemu_isspace(rs->cmd_buf[start])) {
            break;
        }

        --start;
    }

    /* find first space (backwards) */
    while (start > 0) {
        if (qemu_isspace(rs->cmd_buf[start])) {
            ++start;
            break;
        }

        --start;
    }

    /* remove word */
    if (start < rs->cmd_buf_index) {
        memmove(rs->cmd_buf + start,
                rs->cmd_buf + rs->cmd_buf_index,
                rs->cmd_buf_size - rs->cmd_buf_index);
        rs->cmd_buf_size -= rs->cmd_buf_index - start;
        rs->cmd_buf_index = start;
    }
}

static void readline_bol(ReadLineState *rs)
{
    rs->cmd_buf_index = 0;
}

static void readline_eol(ReadLineState *rs)
{
    rs->cmd_buf_index = rs->cmd_buf_size;
}

static void readline_up_char(ReadLineState *rs)
{
    int idx;

    if (rs->hist_entry == 0)
	return;
    if (rs->hist_entry == -1) {
	/* Find latest entry */
	for (idx = 0; idx < READLINE_MAX_CMDS; idx++) {
	    if (rs->history[idx] == NULL)
		break;
	}
	rs->hist_entry = idx;
    }
    rs->hist_entry--;
    if (rs->hist_entry >= 0) {
	pstrcpy(rs->cmd_buf, sizeof(rs->cmd_buf),
                rs->history[rs->hist_entry]);
	rs->cmd_buf_index = rs->cmd_buf_size = strlen(rs->cmd_buf);
    }
}

static void readline_down_char(ReadLineState *rs)
{
    if (rs->hist_entry == READLINE_MAX_CMDS - 1 || rs->hist_entry == -1)
	return;
    if (rs->history[++rs->hist_entry] != NULL) {
	pstrcpy(rs->cmd_buf, sizeof(rs->cmd_buf),
                rs->history[rs->hist_entry]);
    } else {
	rs->hist_entry = -1;
    }
    rs->cmd_buf_index = rs->cmd_buf_size = strlen(rs->cmd_buf);
}

static void readline_hist_add(ReadLineState *rs, const char *cmdline)
{
    char *hist_entry, *new_entry;
    int idx;

    if (cmdline[0] == '\0')
	return;
    new_entry = NULL;
    if (rs->hist_entry != -1) {
	/* We were editing an existing history entry: replace it */
	hist_entry = rs->history[rs->hist_entry];
	idx = rs->hist_entry;
	if (strcmp(hist_entry, cmdline) == 0) {
	    goto same_entry;
	}
    }
    /* Search cmdline in history buffers */
    for (idx = 0; idx < READLINE_MAX_CMDS; idx++) {
	hist_entry = rs->history[idx];
	if (hist_entry == NULL)
	    break;
	if (strcmp(hist_entry, cmdline) == 0) {
	same_entry:
	    new_entry = hist_entry;
	    /* Put this entry at the end of history */
	    memmove(&rs->history[idx], &rs->history[idx + 1],
		    (READLINE_MAX_CMDS - idx + 1) * sizeof(char *));
	    rs->history[READLINE_MAX_CMDS - 1] = NULL;
	    for (; idx < READLINE_MAX_CMDS; idx++) {
		if (rs->history[idx] == NULL)
		    break;
	    }
	    break;
	}
    }
    if (idx == READLINE_MAX_CMDS) {
	/* Need to get one free slot */
	free(rs->history[0]);
	memcpy(rs->history, &rs->history[1],
	       (READLINE_MAX_CMDS - 1) * sizeof(char *));
	rs->history[READLINE_MAX_CMDS - 1] = NULL;
	idx = READLINE_MAX_CMDS - 1;
    }
    if (new_entry == NULL)
	new_entry = strdup(cmdline);
    rs->history[idx] = new_entry;
    rs->hist_entry = -1;
}

/* completion support */

void readline_add_completion(ReadLineState *rs, const char *str)
{
    if (rs->nb_completions < READLINE_MAX_COMPLETIONS) {
        rs->completions[rs->nb_completions++] = qemu_strdup(str);
    }
}

void readline_set_completion_index(ReadLineState *rs, int index)
{
    rs->completion_index = index;
}

static void readline_completion(ReadLineState *rs)
{
    Monitor *mon = cur_mon;
    int len, i, j, max_width, nb_cols, max_prefix;
    char *cmdline;

    rs->nb_completions = 0;

    cmdline = qemu_malloc(rs->cmd_buf_index + 1);
    memcpy(cmdline, rs->cmd_buf, rs->cmd_buf_index);
    cmdline[rs->cmd_buf_index] = '\0';
    rs->completion_finder(cmdline);
    qemu_free(cmdline);

    /* no completion found */
    if (rs->nb_completions <= 0)
        return;
    if (rs->nb_completions == 1) {
        len = strlen(rs->completions[0]);
        for(i = rs->completion_index; i < len; i++) {
            readline_insert_char(rs, rs->completions[0][i]);
        }
        /* extra space for next argument. XXX: make it more generic */
        if (len > 0 && rs->completions[0][len - 1] != '/')
            readline_insert_char(rs, ' ');
    } else {
        monitor_printf(mon, "\n");
        max_width = 0;
        max_prefix = 0;	
        for(i = 0; i < rs->nb_completions; i++) {
            len = strlen(rs->completions[i]);
            if (i==0) {
                max_prefix = len;
            } else {
                if (len < max_prefix)
                    max_prefix = len;
                for(j=0; j<max_prefix; j++) {
                    if (rs->completions[i][j] != rs->completions[0][j])
                        max_prefix = j;
                }
            }
            if (len > max_width)
                max_width = len;
        }
        if (max_prefix > 0) 
            for(i = rs->completion_index; i < max_prefix; i++) {
                readline_insert_char(rs, rs->completions[0][i]);
            }
        max_width += 2;
        if (max_width < 10)
            max_width = 10;
        else if (max_width > 80)
            max_width = 80;
        nb_cols = 80 / max_width;
        j = 0;
        for(i = 0; i < rs->nb_completions; i++) {
            monitor_printf(rs->mon, "%-*s", max_width, rs->completions[i]);
            if (++j == nb_cols || i == (rs->nb_completions - 1)) {
                monitor_printf(rs->mon, "\n");
                j = 0;
            }
        }
        readline_show_prompt(rs);
    }
}

/* return true if command handled */
void readline_handle_byte(ReadLineState *rs, int ch)
{
    switch(rs->esc_state) {
    case IS_NORM:
        switch(ch) {
        case 1:
            readline_bol(rs);
            break;
        case 4:
            readline_delete_char(rs);
            break;
        case 5:
            readline_eol(rs);
            break;
        case 9:
            readline_completion(rs);
            break;
        case 10:
        case 13:
            rs->cmd_buf[rs->cmd_buf_size] = '\0';
            if (!rs->read_password)
                readline_hist_add(rs, rs->cmd_buf);
            monitor_printf(rs->mon, "\n");
            rs->cmd_buf_index = 0;
            rs->cmd_buf_size = 0;
            rs->last_cmd_buf_index = 0;
            rs->last_cmd_buf_size = 0;
            rs->readline_func(rs->mon, rs->cmd_buf, rs->readline_opaque);
            break;
        case 23:
            /* ^W */
            readline_backword(rs);
            break;
        case 27:
            rs->esc_state = IS_ESC;
            break;
        case 127:
        case 8:
            readline_backspace(rs);
            break;
	case 155:
            rs->esc_state = IS_CSI;
	    break;
        default:
            if (ch >= 32) {
                readline_insert_char(rs, ch);
            }
            break;
        }
        break;
    case IS_ESC:
        if (ch == '[') {
            rs->esc_state = IS_CSI;
            rs->esc_param = 0;
        } else {
            rs->esc_state = IS_NORM;
        }
        break;
    case IS_CSI:
        switch(ch) {
	case 'A':
	case 'F':
	    readline_up_char(rs);
	    break;
	case 'B':
	case 'E':
	    readline_down_char(rs);
	    break;
        case 'D':
            readline_backward_char(rs);
            break;
        case 'C':
            readline_forward_char(rs);
            break;
        case '0' ... '9':
            rs->esc_param = rs->esc_param * 10 + (ch - '0');
            goto the_end;
        case '~':
            switch(rs->esc_param) {
            case 1:
                readline_bol(rs);
                break;
            case 3:
                readline_delete_char(rs);
                break;
            case 4:
                readline_eol(rs);
                break;
            }
            break;
        default:
            break;
        }
        rs->esc_state = IS_NORM;
    the_end:
        break;
    }
    readline_update(rs);
}

void readline_start(ReadLineState *rs, const char *prompt, int read_password,
                    ReadLineFunc *readline_func, void *opaque)
{
    pstrcpy(rs->prompt, sizeof(rs->prompt), prompt);
    rs->readline_func = readline_func;
    rs->readline_opaque = opaque;
    rs->read_password = read_password;
    readline_restart(rs);
}

void readline_restart(ReadLineState *rs)
{
    rs->cmd_buf_index = 0;
    rs->cmd_buf_size = 0;
}

const char *readline_get_history(ReadLineState *rs, unsigned int index)
{
    if (index >= READLINE_MAX_CMDS)
        return NULL;
    return rs->history[index];
}

ReadLineState *readline_init(Monitor *mon,
                             ReadLineCompletionFunc *completion_finder)
{
    ReadLineState *rs = qemu_mallocz(sizeof(*rs));

    if (!rs)
        return NULL;

    rs->hist_entry = -1;
    rs->mon = mon;
    rs->completion_finder = completion_finder;

    return rs;
}
