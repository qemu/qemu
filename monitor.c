/*
 * QEMU monitor
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <malloc.h>
#include <termios.h>
#include <sys/poll.h>
#include <errno.h>
#include <ctype.h>

#include "cpu.h"
#include "vl.h"

//#define DEBUG

#define TERM_CMD_BUF_SIZE 4095
#define MAX_ARGS 64

#define IS_NORM 0
#define IS_ESC  1
#define IS_CSI  2

#define printf do_not_use_printf

static char term_cmd_buf[TERM_CMD_BUF_SIZE + 1];
static int term_cmd_buf_index;
static int term_cmd_buf_size;
static int term_esc_state;
static int term_esc_param;

typedef struct term_cmd_t {
    const char *name;
    void (*handler)(int argc, const char **argv);
    const char *params;
    const char *help;
} term_cmd_t;

static term_cmd_t term_cmds[];
static term_cmd_t info_cmds[];

void term_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void term_flush(void)
{
    fflush(stdout);
}

static int compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for(;;) {
        pstart = p;
        p = strchr(p, '|');
        if (!p)
            p = pstart + strlen(pstart);
        if ((p - pstart) == len && !memcmp(pstart, name, len))
            return 1;
        if (*p == '\0')
            break;
        p++;
    }
    return 0;
}

static void help_cmd1(term_cmd_t *cmds, const char *prefix, const char *name)
{
    term_cmd_t *cmd;

    for(cmd = cmds; cmd->name != NULL; cmd++) {
        if (!name || !strcmp(name, cmd->name))
            term_printf("%s%s %s -- %s\n", prefix, cmd->name, cmd->params, cmd->help);
    }
}

static void help_cmd(const char *name)
{
    if (name && !strcmp(name, "info")) {
        help_cmd1(info_cmds, "info ", NULL);
    } else {
        help_cmd1(term_cmds, "", name);
        if (name && !strcmp(name, "log")) {
            CPULogItem *item;
            term_printf("Log items (comma separated):\n");
            term_printf("%-10s %s\n", "none", "remove all logs");
            for(item = cpu_log_items; item->mask != 0; item++) {
                term_printf("%-10s %s\n", item->name, item->help);
            }
        }
    }
}

static void do_help(int argc, const char **argv)
{
    help_cmd(argv[1]);
}

static void do_commit(int argc, const char **argv)
{
    int i;

    for (i = 0; i < MAX_DISKS; i++) {
        if (bs_table[i])
            bdrv_commit(bs_table[i]);
    }
}

static void do_info(int argc, const char **argv)
{
    term_cmd_t *cmd;
    const char *item;

    if (argc < 2)
        goto help;
    item = argv[1];
    for(cmd = info_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(argv[1], cmd->name)) 
            goto found;
    }
 help:
    help_cmd(argv[0]);
    return;
 found:
    cmd->handler(argc, argv);
}

static void do_info_network(int argc, const char **argv)
{
    int i, j;
    NetDriverState *nd;
    
    for(i = 0; i < nb_nics; i++) {
        nd = &nd_table[i];
        term_printf("%d: ifname=%s macaddr=", i, nd->ifname);
        for(j = 0; j < 6; j++) {
            if (j > 0)
                term_printf(":");
            term_printf("%02x", nd->macaddr[j]);
        }
        term_printf("\n");
    }
}
 
static void do_info_block(int argc, const char **argv)
{
    bdrv_info();
}

static void do_quit(int argc, const char **argv)
{
    exit(0);
}

static int eject_device(BlockDriverState *bs, int force)
{
    if (bdrv_is_inserted(bs)) {
        if (!force) {
            if (!bdrv_is_removable(bs)) {
                term_printf("device is not removable\n");
                return -1;
            }
            if (bdrv_is_locked(bs)) {
                term_printf("device is locked\n");
                return -1;
            }
        }
        bdrv_close(bs);
    }
    return 0;
}

static void do_eject(int argc, const char **argv)
{
    BlockDriverState *bs;
    const char **parg;
    int force;

    parg = argv + 1;
    if (!*parg) {
    fail:
        help_cmd(argv[0]);
        return;
    }
    force = 0;
    if (!strcmp(*parg, "-f")) {
        force = 1;
        parg++;
    }
    if (!*parg)
        goto fail;
    bs = bdrv_find(*parg);
    if (!bs) {
        term_printf("device not found\n");
        return;
    }
    eject_device(bs, force);
}

static void do_change(int argc, const char **argv)
{
    BlockDriverState *bs;

    if (argc != 3) {
        help_cmd(argv[0]);
        return;
    }
    bs = bdrv_find(argv[1]);
    if (!bs) {
        term_printf("device not found\n");
        return;
    }
    if (eject_device(bs, 0) < 0)
        return;
    bdrv_open(bs, argv[2], 0);
}

static void do_screen_dump(int argc, const char **argv)
{
    if (argc != 2) {
        help_cmd(argv[0]);
        return;
    }
    vga_screen_dump(argv[1]);
}

static void do_log(int argc, const char **argv)
{
    int mask;
    
    if (argc != 2)
        goto help;
    if (!strcmp(argv[1], "none")) {
        mask = 0;
    } else {
        mask = cpu_str_to_log_mask(argv[1]);
        if (!mask) {
        help:
            help_cmd(argv[0]);
            return;
        }
    }
    cpu_set_log(mask);
}

static void do_savevm(int argc, const char **argv)
{
    if (argc != 2) {
        help_cmd(argv[0]);
        return;
    }
    if (qemu_savevm(argv[1]) < 0)
        term_printf("I/O error when saving VM to '%s'\n", argv[1]);
}

static void do_loadvm(int argc, const char **argv)
{
    if (argc != 2) {
        help_cmd(argv[0]);
        return;
    }
    if (qemu_loadvm(argv[1]) < 0) 
        term_printf("I/O error when loading VM from '%s'\n", argv[1]);
}

static void do_stop(int argc, const char **argv)
{
    vm_stop(EXCP_INTERRUPT);
}

static void do_cont(int argc, const char **argv)
{
    vm_start();
}

static void do_gdbserver(int argc, const char **argv)
{
    int port;

    port = DEFAULT_GDBSTUB_PORT;
    if (argc >= 2)
        port = atoi(argv[1]);
    if (gdbserver_start(port) < 0) {
        qemu_printf("Could not open gdbserver socket on port %d\n", port);
    } else {
        qemu_printf("Waiting gdb connection on port %d\n", port);
    }
}

static term_cmd_t term_cmds[] = {
    { "help|?", do_help, 
      "[cmd]", "show the help" },
    { "commit", do_commit, 
      "", "commit changes to the disk images (if -snapshot is used)" },
    { "info", do_info,
      "subcommand", "show various information about the system state" },
    { "q|quit", do_quit,
      "", "quit the emulator" },
    { "eject", do_eject,
      "[-f] device", "eject a removable media (use -f to force it)" },
    { "change", do_change,
      "device filename", "change a removable media" },
    { "screendump", do_screen_dump, 
      "filename", "save screen into PPM image 'filename'" },
    { "log", do_log,
      "item1[,...]", "activate logging of the specified items to '/tmp/qemu.log'" }, 
    { "savevm", do_savevm,
      "filename", "save the whole virtual machine state to 'filename'" }, 
    { "loadvm", do_loadvm,
      "filename", "restore the whole virtual machine state from 'filename'" }, 
    { "stop", do_stop, "", "stop emulation", },
    { "c|cont", do_cont, "", "resume emulation", },
    { "gdbserver", do_gdbserver, "[port]", "start gdbserver session (default port=1234)", },
    { NULL, NULL, }, 
};

static term_cmd_t info_cmds[] = {
    { "network", do_info_network,
      "", "show the network state" },
    { "block", do_info_block,
      "", "show the block devices" },
    { NULL, NULL, },
};

static void term_handle_command(char *cmdline)
{
    char *p, *pstart;
    int argc;
    const char *args[MAX_ARGS + 1];
    term_cmd_t *cmd;

#ifdef DEBUG
    term_printf("command='%s'\n", cmdline);
#endif
    
    /* split command in words */
    argc = 0;
    p = cmdline;
    for(;;) {
        while (isspace(*p))
            p++;
        if (*p == '\0')
            break;
        pstart = p;
        while (*p != '\0' && !isspace(*p))
            p++;
        args[argc] = pstart;
        argc++;
        if (argc >= MAX_ARGS)
            break;
        if (*p == '\0')
            break;
        *p++ = '\0';
    }
    args[argc] = NULL;
#ifdef DEBUG
    for(i=0;i<argc;i++) {
        term_printf(" '%s'", args[i]);
    }
    term_printf("\n");
#endif
    if (argc <= 0)
        return;
    for(cmd = term_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(args[0], cmd->name)) 
            goto found;
    }
    term_printf("unknown command: '%s'\n", args[0]);
    return;
 found:
    cmd->handler(argc, args);
}

static void term_show_prompt(void)
{
    term_printf("(qemu) ");
    fflush(stdout);
    term_cmd_buf_index = 0;
    term_cmd_buf_size = 0;
    term_esc_state = IS_NORM;
}

static void term_insert_char(int ch)
{
    if (term_cmd_buf_index < TERM_CMD_BUF_SIZE) {
        memmove(term_cmd_buf + term_cmd_buf_index + 1,
                term_cmd_buf + term_cmd_buf_index,
                term_cmd_buf_size - term_cmd_buf_index);
        term_cmd_buf[term_cmd_buf_index] = ch;
        term_cmd_buf_size++;
        term_printf("\033[@%c", ch);
        term_cmd_buf_index++;
        term_flush();
    }
}

static void term_backward_char(void)
{
    if (term_cmd_buf_index > 0) {
        term_cmd_buf_index--;
        term_printf("\033[D");
        term_flush();
    }
}

static void term_forward_char(void)
{
    if (term_cmd_buf_index < term_cmd_buf_size) {
        term_cmd_buf_index++;
        term_printf("\033[C");
        term_flush();
    }
}

static void term_delete_char(void)
{
    if (term_cmd_buf_index < term_cmd_buf_size) {
        memmove(term_cmd_buf + term_cmd_buf_index,
                term_cmd_buf + term_cmd_buf_index + 1,
                term_cmd_buf_size - term_cmd_buf_index - 1);
        term_printf("\033[P");
        term_cmd_buf_size--;
        term_flush();
    }
}

static void term_backspace(void)
{
    if (term_cmd_buf_index > 0) {
        term_backward_char();
        term_delete_char();
    }
}

static void term_bol(void)
{
    while (term_cmd_buf_index > 0)
        term_backward_char();
}

static void term_eol(void)
{
    while (term_cmd_buf_index < term_cmd_buf_size)
        term_forward_char();
}

/* return true if command handled */
static void term_handle_byte(int ch)
{
    switch(term_esc_state) {
    case IS_NORM:
        switch(ch) {
        case 1:
            term_bol();
            break;
        case 5:
            term_eol();
            break;
        case 10:
        case 13:
            term_cmd_buf[term_cmd_buf_size] = '\0';
            term_printf("\n");
            term_handle_command(term_cmd_buf);
            term_show_prompt();
            break;
        case 27:
            term_esc_state = IS_ESC;
            break;
        case 127:
        case 8:
            term_backspace();
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
}

/*************************************************************/
/* serial console support */

#define TERM_ESCAPE 0x01 /* ctrl-a is used for escape */

static int term_got_escape, term_command;

void term_print_help(void)
{
    term_printf("\n"
                "C-a h    print this help\n"
                "C-a x    exit emulatior\n"
                "C-a s    save disk data back to file (if -snapshot)\n"
                "C-a b    send break (magic sysrq)\n"
                "C-a c    switch between console and monitor\n"
                "C-a C-a  send C-a\n"
                );
}

/* called when a char is received */
static void term_received_byte(int ch)
{
    if (!serial_console) {
        /* if no serial console, handle every command */
        term_handle_byte(ch);
    } else {
        if (term_got_escape) {
            term_got_escape = 0;
            switch(ch) {
            case 'h':
                term_print_help();
                break;
            case 'x':
                exit(0);
                break;
            case 's': 
                {
                    int i;
                    for (i = 0; i < MAX_DISKS; i++) {
                        if (bs_table[i])
                            bdrv_commit(bs_table[i]);
                    }
                }
                break;
            case 'b':
                if (serial_console)
                    serial_receive_break(serial_console);
                break;
            case 'c':
                if (!term_command) {
                    term_show_prompt();
                    term_command = 1;
                } else {
                    term_command = 0;
                }
                break;
            case TERM_ESCAPE:
                goto send_char;
            }
        } else if (ch == TERM_ESCAPE) {
            term_got_escape = 1;
        } else {
        send_char:
            if (term_command) {
                term_handle_byte(ch);
            } else {
                if (serial_console)
                    serial_receive_byte(serial_console, ch);
            }
        }
    }
}

static int term_can_read(void *opaque)
{
    if (serial_console) {
        return serial_can_receive(serial_console);
    } else {
        return 128;
    }
}

static void term_read(void *opaque, const uint8_t *buf, int size)
{
    int i;
    for(i = 0; i < size; i++)
        term_received_byte(buf[i]);
}

void monitor_init(void)
{
    if (!serial_console) {
        term_printf("QEMU %s monitor - type 'help' for more information\n",
                    QEMU_VERSION);
        term_show_prompt();
    }
    qemu_add_fd_read_handler(0, term_can_read, term_read, NULL);
}
