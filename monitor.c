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
#include "vl.h"
#include "disas.h"

//#define DEBUG

#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif

#define TERM_CMD_BUF_SIZE 4095

#define IS_NORM 0
#define IS_ESC  1
#define IS_CSI  2

#define printf do_not_use_printf

static char term_cmd_buf[TERM_CMD_BUF_SIZE + 1];
static int term_cmd_buf_index;
static int term_cmd_buf_size;
static int term_esc_state;
static int term_esc_param;

/*
 * Supported types:
 * 
 * 'F'          filename
 * 's'          string (accept optional quote)
 * 'i'          integer
 * '/'          optional gdb-like print format (like "/10x")
 *
 * '?'          optional type (for 'F', 's' and 'i')
 *
 */

typedef struct term_cmd_t {
    const char *name;
    const char *args_type;
    void (*handler)();
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

static void do_help(const char *name)
{
    help_cmd(name);
}

static void do_commit(void)
{
    int i;

    for (i = 0; i < MAX_DISKS; i++) {
        if (bs_table[i])
            bdrv_commit(bs_table[i]);
    }
}

static void do_info(const char *item)
{
    term_cmd_t *cmd;

    if (!item)
        goto help;
    for(cmd = info_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(item, cmd->name)) 
            goto found;
    }
 help:
    help_cmd("info");
    return;
 found:
    cmd->handler();
}

static void do_info_network(void)
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
 
static void do_info_block(void)
{
    bdrv_info();
}

static void do_info_registers(void)
{
#ifdef TARGET_I386
    cpu_dump_state(cpu_single_env, stdout, X86_DUMP_FPU | X86_DUMP_CCOP);
#else
    cpu_dump_state(cpu_single_env, stdout, 0);
#endif
}

static void do_quit(void)
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

static void do_eject(int force, const char *filename)
{
    BlockDriverState *bs;

    term_printf("%d %s\n", force, filename);

    bs = bdrv_find(filename);
    if (!bs) {
        term_printf("device not found\n");
        return;
    }
    eject_device(bs, force);
}

static void do_change(const char *device, const char *filename)
{
    BlockDriverState *bs;

    bs = bdrv_find(device);
    if (!bs) {
        term_printf("device not found\n");
        return;
    }
    if (eject_device(bs, 0) < 0)
        return;
    bdrv_open(bs, filename, 0);
}

static void do_screen_dump(const char *filename)
{
    vga_screen_dump(filename);
}

static void do_log(const char *items)
{
    int mask;
    
    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = cpu_str_to_log_mask(items);
        if (!mask) {
            help_cmd("log");
            return;
        }
    }
    cpu_set_log(mask);
}

static void do_savevm(const char *filename)
{
    if (qemu_savevm(filename) < 0)
        term_printf("I/O error when saving VM to '%s'\n", filename);
}

static void do_loadvm(const char *filename)
{
    if (qemu_loadvm(filename) < 0) 
        term_printf("I/O error when loading VM from '%s'\n", filename);
}

static void do_stop(void)
{
    vm_stop(EXCP_INTERRUPT);
}

static void do_cont(void)
{
    vm_start();
}

#ifdef CONFIG_GDBSTUB
static void do_gdbserver(int has_port, int port)
{
    if (!has_port)
        port = DEFAULT_GDBSTUB_PORT;
    if (gdbserver_start(port) < 0) {
        qemu_printf("Could not open gdbserver socket on port %d\n", port);
    } else {
        qemu_printf("Waiting gdb connection on port %d\n", port);
    }
}
#endif

static void term_printc(int c)
{
    term_printf("'");
    switch(c) {
    case '\'':
        term_printf("\\'");
        break;
    case '\\':
        term_printf("\\\\");
        break;
    case '\n':
        term_printf("\\n");
        break;
    case '\r':
        term_printf("\\r");
        break;
    default:
        if (c >= 32 && c <= 126) {
            term_printf("%c", c);
        } else {
            term_printf("\\x%02x", c);
        }
        break;
    }
    term_printf("'");
}

static void memory_dump(int count, int format, int wsize, 
                        target_ulong addr, int is_physical)
{
    int nb_per_line, l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;

    if (format == 'i') {
        int flags;
        flags = 0;
#ifdef TARGET_I386
        /* we use the current CS size */
        if (!(cpu_single_env->segs[R_CS].flags & DESC_B_MASK))
            flags = 1;
#endif        
        monitor_disas(addr, count, is_physical, flags);
        return;
    }

    len = wsize * count;
    if (wsize == 1)
        line_size = 8;
    else
        line_size = 16;
    nb_per_line = line_size / wsize;
    max_digits = 0;

    switch(format) {
    case 'o':
        max_digits = (wsize * 8 + 2) / 3;
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = (wsize * 8 * 10 + 32) / 33;
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        term_printf("0x%08x:", addr);
        l = len;
        if (l > line_size)
            l = line_size;
        if (is_physical) {
            cpu_physical_memory_rw(addr, buf, l, 0);
        } else {
            cpu_memory_rw_debug(cpu_single_env, addr, buf, l, 0);
        }
        i = 0; 
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_raw(buf + i);
                break;
            case 2:
                v = lduw_raw(buf + i);
                break;
            case 4:
                v = ldl_raw(buf + i);
                break;
            case 8:
                v = ldq_raw(buf + i);
                break;
            }
            term_printf(" ");
            switch(format) {
            case 'o':
                term_printf("%#*llo", max_digits, v);
                break;
            case 'x':
                term_printf("0x%0*llx", max_digits, v);
                break;
            case 'u':
                term_printf("%*llu", max_digits, v);
                break;
            case 'd':
                term_printf("%*lld", max_digits, v);
                break;
            case 'c':
                term_printc(v);
                break;
            }
            i += wsize;
        }
        term_printf("\n");
        addr += l;
        len -= l;
    }
}

static void do_memory_dump(int count, int format, int size, int addr)
{
    memory_dump(count, format, size, addr, 0);
}

static void do_physical_memory_dump(int count, int format, int size, int addr)
{
    memory_dump(count, format, size, addr, 1);
}

static void do_print(int count, int format, int size, int val)
{
    switch(format) {
    case 'o':
        term_printf("%#o", val);
        break;
    case 'x':
        term_printf("%#x", val);
        break;
    case 'u':
        term_printf("%u", val);
        break;
    default:
    case 'd':
        term_printf("%d", val);
        break;
    case 'c':
        term_printc(val);
        break;
    }
    term_printf("\n");
}

static term_cmd_t term_cmds[] = {
    { "help|?", "s?", do_help, 
      "[cmd]", "show the help" },
    { "commit", "", do_commit, 
      "", "commit changes to the disk images (if -snapshot is used)" },
    { "info", "s?", do_info,
      "subcommand", "show various information about the system state" },
    { "q|quit", "", do_quit,
      "", "quit the emulator" },
    { "eject", "-fs", do_eject,
      "[-f] device", "eject a removable media (use -f to force it)" },
    { "change", "sF", do_change,
      "device filename", "change a removable media" },
    { "screendump", "F", do_screen_dump, 
      "filename", "save screen into PPM image 'filename'" },
    { "log", "s", do_log,
      "item1[,...]", "activate logging of the specified items to '/tmp/qemu.log'" }, 
    { "savevm", "F", do_savevm,
      "filename", "save the whole virtual machine state to 'filename'" }, 
    { "loadvm", "F", do_loadvm,
      "filename", "restore the whole virtual machine state from 'filename'" }, 
    { "stop", "", do_stop, 
      "", "stop emulation", },
    { "c|cont", "", do_cont, 
      "", "resume emulation", },
#ifdef CONFIG_GDBSTUB
    { "gdbserver", "i?", do_gdbserver, 
      "[port]", "start gdbserver session (default port=1234)", },
#endif
    { "x", "/i", do_memory_dump, 
      "/fmt addr", "virtual memory dump starting at 'addr'", },
    { "xp", "/i", do_physical_memory_dump, 
      "/fmt addr", "physical memory dump starting at 'addr'", },
    { "p|print", "/i", do_print, 
      "/fmt expr", "print expression value (use $reg for CPU register access)", },
    { NULL, NULL, }, 
};

static term_cmd_t info_cmds[] = {
    { "network", "", do_info_network,
      "", "show the network state" },
    { "block", "", do_info_block,
      "", "show the block devices" },
    { "registers", "", do_info_registers,
      "", "show the cpu registers" },
    { NULL, NULL, },
};

/*******************************************************************/

static const char *pch;
static jmp_buf expr_env;

typedef struct MonitorDef {
    const char *name;
    int offset;
    int (*get_value)(struct MonitorDef *md);
} MonitorDef;

static MonitorDef monitor_defs[] = {
#ifdef TARGET_I386
    { "eax", offsetof(CPUState, regs[0]) },
    { "ecx", offsetof(CPUState, regs[1]) },
    { "edx", offsetof(CPUState, regs[2]) },
    { "ebx", offsetof(CPUState, regs[3]) },
    { "esp|sp", offsetof(CPUState, regs[4]) },
    { "ebp|fp", offsetof(CPUState, regs[5]) },
    { "esi", offsetof(CPUState, regs[6]) },
    { "esi", offsetof(CPUState, regs[7]) },
    { "eflags", offsetof(CPUState, eflags) },
    { "eip|pc", offsetof(CPUState, eip) },
#endif
    { NULL },
};

static void expr_error(const char *fmt) 
{
    term_printf(fmt);
    term_printf("\n");
    longjmp(expr_env, 1);
}

static int get_monitor_def(int *pval, const char *name)
{
    MonitorDef *md;
    for(md = monitor_defs; md->name != NULL; md++) {
        if (compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(md);
            } else {
                *pval = *(uint32_t *)((uint8_t *)cpu_single_env + md->offset);
            }
            return 0;
        }
    }
    return -1;
}

static void next(void)
{
    if (pch != '\0') {
        pch++;
        while (isspace(*pch))
            pch++;
    }
}

static int expr_sum(void);

static int expr_unary(void)
{
    int n;
    char *p;

    switch(*pch) {
    case '+':
        next();
        n = expr_unary();
        break;
    case '-':
        next();
        n = -expr_unary();
        break;
    case '~':
        next();
        n = ~expr_unary();
        break;
    case '(':
        next();
        n = expr_sum();
        if (*pch != ')') {
            expr_error("')' expected");
        }
        next();
        break;
    case '$':
        {
            char buf[128], *q;
            
            pch++;
            q = buf;
            while ((*pch >= 'a' && *pch <= 'z') ||
                   (*pch >= 'A' && *pch <= 'Z') ||
                   (*pch >= '0' && *pch <= '9') ||
                   *pch == '_') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *pch;
                pch++;
            }
            while (isspace(*pch))
                pch++;
            *q = 0;
            if (get_monitor_def(&n, buf))
                expr_error("unknown register");
        }
        break;
    case '\0':
        expr_error("unexpected end of expression");
        n = 0;
        break;
    default:
        n = strtoul(pch, &p, 0);
        if (pch == p) {
            expr_error("invalid char in expression");
        }
        pch = p;
        while (isspace(*pch))
            pch++;
        break;
    }
    return n;
}


static int expr_prod(void)
{
    int val, val2, op;

    val = expr_unary();
    for(;;) {
        op = *pch;
        if (op != '*' && op != '/' && op != '%')
            break;
        next();
        val2 = expr_unary();
        switch(op) {
        default:
        case '*':
            val *= val2;
            break;
        case '/':
        case '%':
            if (val2 == 0) 
                expr_error("divison by zero");
            if (op == '/')
                val /= val2;
            else
                val %= val2;
            break;
        }
    }
    return val;
}

static int expr_logic(void)
{
    int val, val2, op;

    val = expr_prod();
    for(;;) {
        op = *pch;
        if (op != '&' && op != '|' && op != '^')
            break;
        next();
        val2 = expr_prod();
        switch(op) {
        default:
        case '&':
            val &= val2;
            break;
        case '|':
            val |= val2;
            break;
        case '^':
            val ^= val2;
            break;
        }
    }
    return val;
}

static int expr_sum(void)
{
    int val, val2, op;

    val = expr_logic();
    for(;;) {
        op = *pch;
        if (op != '+' && op != '-')
            break;
        next();
        val2 = expr_logic();
        if (op == '+')
            val += val2;
        else
            val -= val2;
    }
    return val;
}

static int get_expr(int *pval, const char **pp)
{
    pch = *pp;
    if (setjmp(expr_env)) {
        *pp = pch;
        return -1;
    }
    while (isspace(*pch))
        pch++;
    *pval = expr_sum();
    *pp = pch;
    return 0;
}

static int get_str(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int c;

    p = *pp;
    while (isspace(*p))
        p++;
    if (*p == '\0') {
    fail:
        *pp = p;
        return -1;
    }
    q = buf;
    if (*p == '\"') {
        p++;
        while (*p != '\0' && *p != '\"') {
            if (*p == '\\') {
                p++;
                c = *p++;
                switch(c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '\\':
                case '\'':
                case '\"':
                    break;
                default:
                    qemu_printf("unsupported escape code: '\\%c'\n", c);
                    goto fail;
                }
                if ((q - buf) < buf_size - 1) {
                    *q++ = c;
                }
            } else {
                if ((q - buf) < buf_size - 1) {
                    *q++ = *p;
                }
                p++;
            }
        }
        if (*p != '\"') {
            qemu_printf("untermintated string\n");
            goto fail;
        }
        p++;
    } else {
        while (*p != '\0' && !isspace(*p)) {
            if ((q - buf) < buf_size - 1) {
                *q++ = *p;
            }
            p++;
        }
        *q = '\0';
    }
    *pp = p;
    return 0;
}

static int default_fmt_format = 'x';
static int default_fmt_size = 4;

#define MAX_ARGS 16

static void term_handle_command(const char *cmdline)
{
    const char *p, *pstart, *typestr;
    char *q;
    int c, nb_args, len, i, has_arg;
    term_cmd_t *cmd;
    char cmdname[256];
    char buf[1024];
    void *str_allocated[MAX_ARGS];
    void *args[MAX_ARGS];

#ifdef DEBUG
    term_printf("command='%s'\n", cmdline);
#endif
    
    /* extract the command name */
    p = cmdline;
    q = cmdname;
    while (isspace(*p))
        p++;
    if (*p == '\0')
        return;
    pstart = p;
    while (*p != '\0' && *p != '/' && !isspace(*p))
        p++;
    len = p - pstart;
    if (len > sizeof(cmdname) - 1)
        len = sizeof(cmdname) - 1;
    memcpy(cmdname, pstart, len);
    cmdname[len] = '\0';
    
    /* find the command */
    for(cmd = term_cmds; cmd->name != NULL; cmd++) {
        if (compare_cmd(cmdname, cmd->name)) 
            goto found;
    }
    term_printf("unknown command: '%s'\n", cmdname);
    return;
 found:

    for(i = 0; i < MAX_ARGS; i++)
        str_allocated[i] = NULL;
    
    /* parse the parameters */
    typestr = cmd->args_type;
    nb_args = 0;
    for(;;) {
        c = *typestr;
        if (c == '\0')
            break;
        typestr++;
        switch(c) {
        case 'F':
        case 's':
            {
                int ret;
                char *str;
                
                while (isspace(*p)) 
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no optional string: NULL argument */
                        str = NULL;
                        goto add_str;
                    }
                }
                ret = get_str(buf, sizeof(buf), &p);
                if (ret < 0) {
                    if (c == 'F')
                        term_printf("%s: filename expected\n", cmdname);
                    else
                        term_printf("%s: string expected\n", cmdname);
                    goto fail;
                }
                str = qemu_malloc(strlen(buf) + 1);
                strcpy(str, buf);
                str_allocated[nb_args] = str;
            add_str:
                if (nb_args >= MAX_ARGS) {
                error_args:
                    term_printf("%s: too many arguments\n", cmdname);
                    goto fail;
                }
                args[nb_args++] = str;
            }
            break;
        case '/':
            {
                int count, format, size;
                
                while (isspace(*p))
                    p++;
                if (*p == '/') {
                    /* format found */
                    p++;
                    count = 1;
                    if (isdigit(*p)) {
                        count = 0;
                        while (isdigit(*p)) {
                            count = count * 10 + (*p - '0');
                            p++;
                        }
                    }
                    size = -1;
                    format = -1;
                    for(;;) {
                        switch(*p) {
                        case 'o':
                        case 'd':
                        case 'u':
                        case 'x':
                        case 'i':
                        case 'c':
                            format = *p++;
                            break;
                        case 'b':
                            size = 1;
                            p++;
                            break;
                        case 'h':
                            size = 2;
                            p++;
                            break;
                        case 'w':
                            size = 4;
                            p++;
                            break;
                        case 'g':
                        case 'L':
                            size = 8;
                            p++;
                            break;
                        default:
                            goto next;
                        }
                    }
                next:
                    if (*p != '\0' && !isspace(*p)) {
                        term_printf("invalid char in format: '%c'\n", *p);
                        goto fail;
                    }
                    if (size < 0)
                        size = default_fmt_size;
                    if (format < 0)
                        format = default_fmt_format;
                    default_fmt_size = size;
                    default_fmt_format = format;
                } else {
                    count = 1;
                    format = default_fmt_format;
                    size = default_fmt_size;
                }
                if (nb_args + 3 > MAX_ARGS)
                    goto error_args;
                args[nb_args++] = (void*)count;
                args[nb_args++] = (void*)format;
                args[nb_args++] = (void*)size;
            }
            break;
        case 'i':
            {
                int val;
                while (isspace(*p)) 
                    p++;
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0')
                        has_arg = 0;
                    else
                        has_arg = 1;
                    if (nb_args >= MAX_ARGS)
                        goto error_args;
                    args[nb_args++] = (void *)has_arg;
                    if (!has_arg) {
                        if (nb_args >= MAX_ARGS)
                            goto error_args;
                        val = -1;
                        goto add_num;
                    }
                }
                if (get_expr(&val, &p))
                    goto fail;
            add_num:
                if (nb_args >= MAX_ARGS)
                    goto error_args;
                args[nb_args++] = (void *)val;
            }
            break;
        case '-':
            {
                int has_option;
                /* option */
                
                c = *typestr++;
                if (c == '\0')
                    goto bad_type;
                while (isspace(*p)) 
                    p++;
                has_option = 0;
                if (*p == '-') {
                    p++;
                    if (*p != c) {
                        term_printf("%s: unsupported option -%c\n", 
                                    cmdname, *p);
                        goto fail;
                    }
                    p++;
                    has_option = 1;
                }
                if (nb_args >= MAX_ARGS)
                    goto error_args;
                args[nb_args++] = (void *)has_option;
            }
            break;
        default:
        bad_type:
            term_printf("%s: unknown type '%c'\n", cmdname, c);
            goto fail;
        }
    }
    /* check that all arguments were parsed */
    while (isspace(*p))
        p++;
    if (*p != '\0') {
        term_printf("%s: extraneous characters at the end of line\n", 
                    cmdname);
        goto fail;
    }

    switch(nb_args) {
    case 0:
        cmd->handler();
        break;
    case 1:
        cmd->handler(args[0]);
        break;
    case 2:
        cmd->handler(args[0], args[1]);
        break;
    case 3:
        cmd->handler(args[0], args[1], args[2]);
        break;
    case 4:
        cmd->handler(args[0], args[1], args[2], args[3]);
        break;
    case 5:
        cmd->handler(args[0], args[1], args[2], args[3], args[4]);
        break;
    default:
        term_printf("unsupported number of arguments: %d\n", nb_args);
        goto fail;
    }
 fail:
    for(i = 0; i < MAX_ARGS; i++)
        qemu_free(str_allocated[i]);
    return;
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
