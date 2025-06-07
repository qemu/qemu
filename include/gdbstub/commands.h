#ifndef GDBSTUB_COMMANDS_H
#define GDBSTUB_COMMANDS_H

typedef void (*GdbCmdHandler)(GArray *params, void *user_ctx);

typedef enum GDBThreadIdKind {
    GDB_ONE_THREAD = 0,
    GDB_ALL_THREADS,     /* One process, all threads */
    GDB_ALL_PROCESSES,
    GDB_READ_THREAD_ERR
} GDBThreadIdKind;

typedef union GdbCmdVariant {
    const char *data;
    uint8_t opcode;
    unsigned long val_ul;
    unsigned long long val_ull;
    struct {
        GDBThreadIdKind kind;
        uint32_t pid;
        uint32_t tid;
    } thread_id;
} GdbCmdVariant;

#define gdb_get_cmd_param(p, i)    (&g_array_index(p, GdbCmdVariant, i))

/**
 * typedef GdbCmdParseEntry - gdb command parser
 *
 * This structure keeps the information necessary to match a gdb command,
 * parse it (extract its parameters), and select the correct handler for it.
 *
 * @cmd: The command to be matched
 * @cmd_startswith: If true, @cmd is compared using startswith
 * @schema: Each schema for the command parameter entry consists of 2 chars,
 * the first char represents the parameter type handling the second char
 * represents the delimiter for the next parameter.
 *
 * Currently supported schema types:
 * 'l' -> unsigned long (stored in .val_ul)
 * 'L' -> unsigned long long (stored in .val_ull)
 * 's' -> string (stored in .data)
 * 'o' -> single char (stored in .opcode)
 * 't' -> thread id (stored in .thread_id)
 * '?' -> skip according to delimiter
 *
 * Currently supported delimiters:
 * '?' -> Stop at any delimiter (",;:=\0")
 * '0' -> Stop at "\0"
 * '.' -> Skip 1 char unless reached "\0"
 * Any other value is treated as the delimiter value itself
 *
 * @allow_stop_reply: True iff the gdbstub can respond to this command with a
 * "stop reply" packet. The list of commands that accept such response is
 * defined at the GDB Remote Serial Protocol documentation. See:
 * https://sourceware.org/gdb/onlinedocs/gdb/Stop-Reply-Packets.html#Stop-Reply-Packets.
 *
 * @need_cpu_context: Pass current CPU context to command handler via user_ctx.
 */
typedef struct GdbCmdParseEntry {
    GdbCmdHandler handler;
    const char *cmd;
    bool cmd_startswith;
    const char *schema;
    bool allow_stop_reply;
    bool need_cpu_context;
} GdbCmdParseEntry;

/**
 * gdb_put_packet() - put string into gdb server's buffer so it is sent
 * to the client
 */
int gdb_put_packet(const char *buf);

/**
 * gdb_extend_query_table() - Extend query table.
 * @table: GPtrArray of GdbCmdParseEntry entries.
 *
 * The caller should free @table afterwards
 */
void gdb_extend_query_table(GPtrArray *table);

/**
 * gdb_extend_set_table() - Extend set table.
 * @table: GPtrArray of GdbCmdParseEntry entries.
 *
 * The caller should free @table afterwards
 */
void gdb_extend_set_table(GPtrArray *table);

/**
 * gdb_extend_qsupported_features() - Extend the qSupported features string.
 * @qsupported_features: The additional qSupported feature(s) string. The string
 * should start with a semicolon and, if there are more than one feature, the
 * features should be separate by a semicolon.
 *
 * The caller should free @qsupported_features afterwards if
 * dynamically allocated.
 */
void gdb_extend_qsupported_features(char *qsupported_features);

/**
 * Convert a hex string to bytes. Conversion is done per byte, so 2 hex digits
 * are converted to 1 byte. Invalid hex digits are treated as 0 digits.
 */
void gdb_hextomem(GByteArray *mem, const char *buf, int len);

#endif /* GDBSTUB_COMMANDS_H */
