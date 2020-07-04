/*
 * QEMU S390 Interactive Boot Menu
 *
 * Copyright 2018 IBM Corp.
 * Author: Collin L. Walling <walling@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "sclp.h"
#include "s390-time.h"

#define KEYCODE_NO_INP '\0'
#define KEYCODE_ESCAPE '\033'
#define KEYCODE_BACKSP '\177'
#define KEYCODE_ENTER  '\r'

/* Offsets from zipl fields to zipl banner start */
#define ZIPL_TIMEOUT_OFFSET 138
#define ZIPL_FLAG_OFFSET    140

#define TOD_CLOCK_MILLISECOND   0x3e8000

#define LOW_CORE_EXTERNAL_INT_ADDR   0x86
#define CLOCK_COMPARATOR_INT         0X1004

static uint8_t flag;
static uint64_t timeout;

static inline void enable_clock_int(void)
{
    uint64_t tmp = 0;

    asm volatile(
        "stctg      0,0,%0\n"
        "oi         6+%0, 0x8\n"
        "lctlg      0,0,%0"
        : : "Q" (tmp) : "memory"
    );
}

static inline void disable_clock_int(void)
{
    uint64_t tmp = 0;

    asm volatile(
        "stctg      0,0,%0\n"
        "ni         6+%0, 0xf7\n"
        "lctlg      0,0,%0"
        : : "Q" (tmp) : "memory"
    );
}

static inline void set_clock_comparator(uint64_t time)
{
    asm volatile("sckc %0" : : "Q" (time));
}

static inline bool check_clock_int(void)
{
    uint16_t *code = (uint16_t *)LOW_CORE_EXTERNAL_INT_ADDR;

    consume_sclp_int();

    return *code == CLOCK_COMPARATOR_INT;
}

static int read_prompt(char *buf, size_t len)
{
    char inp[2] = {};
    uint8_t idx = 0;
    uint64_t time;

    if (timeout) {
        time = get_clock() + timeout * TOD_CLOCK_MILLISECOND;
        set_clock_comparator(time);
        enable_clock_int();
        timeout = 0;
    }

    while (!check_clock_int()) {

        sclp_read(inp, 1); /* Process only one character at a time */

        switch (inp[0]) {
        case KEYCODE_NO_INP:
        case KEYCODE_ESCAPE:
            continue;
        case KEYCODE_BACKSP:
            if (idx > 0) {
                buf[--idx] = 0;
                sclp_print("\b \b");
            }
            continue;
        case KEYCODE_ENTER:
            disable_clock_int();
            return idx;
        default:
            /* Echo input and add to buffer */
            if (idx < len) {
                buf[idx++] = inp[0];
                sclp_print(inp);
            }
        }
    }

    disable_clock_int();
    *buf = 0;

    return 0;
}

static int get_index(void)
{
    char buf[11];
    int len;
    int i;

    memset(buf, 0, sizeof(buf));

    sclp_set_write_mask(SCLP_EVENT_MASK_MSG_ASCII, SCLP_EVENT_MASK_MSG_ASCII);

    len = read_prompt(buf, sizeof(buf) - 1);

    sclp_set_write_mask(0, SCLP_EVENT_MASK_MSG_ASCII);

    /* If no input, boot default */
    if (len == 0) {
        return 0;
    }

    /* Check for erroneous input */
    for (i = 0; i < len; i++) {
        if (!isdigit((unsigned char)buf[i])) {
            return -1;
        }
    }

    return atoui(buf);
}

static void boot_menu_prompt(bool retry)
{
    char tmp[11];

    if (retry) {
        sclp_print("\nError: undefined configuration"
                   "\nPlease choose:\n");
    } else if (timeout > 0) {
        sclp_print("Please choose (default will boot in ");
        sclp_print(uitoa(timeout / 1000, tmp, sizeof(tmp)));
        sclp_print(" seconds):\n");
    } else {
        sclp_print("Please choose:\n");
    }
}

static int get_boot_index(bool *valid_entries)
{
    int boot_index;
    bool retry = false;
    char tmp[5];

    do {
        boot_menu_prompt(retry);
        boot_index = get_index();
        retry = true;
    } while (boot_index < 0 || boot_index >= MAX_BOOT_ENTRIES ||
             !valid_entries[boot_index]);

    sclp_print("\nBooting entry #");
    sclp_print(uitoa(boot_index, tmp, sizeof(tmp)));

    return boot_index;
}

/* Returns the entry number that was printed */
static int zipl_print_entry(const char *data, size_t len)
{
    char buf[len + 2];

    ebcdic_to_ascii(data, buf, len);
    buf[len] = '\n';
    buf[len + 1] = '\0';

    sclp_print(buf);

    return buf[0] == ' ' ? atoui(buf + 1) : atoui(buf);
}

int menu_get_zipl_boot_index(const char *menu_data)
{
    size_t len;
    int entry;
    bool valid_entries[MAX_BOOT_ENTRIES] = {false};
    uint16_t zipl_flag = *(uint16_t *)(menu_data - ZIPL_FLAG_OFFSET);
    uint16_t zipl_timeout = *(uint16_t *)(menu_data - ZIPL_TIMEOUT_OFFSET);

    if (flag == QIPL_FLAG_BM_OPTS_ZIPL) {
        if (!zipl_flag) {
            return 0; /* Boot default */
        }
        /* zipl stores timeout as seconds */
        timeout = zipl_timeout * 1000;
    }

    /* Print banner */
    sclp_print("s390-ccw zIPL Boot Menu\n\n");
    menu_data += strlen(menu_data) + 1;

    /* Print entries */
    while (*menu_data) {
        len = strlen(menu_data);
        entry = zipl_print_entry(menu_data, len);
        menu_data += len + 1;

        valid_entries[entry] = true;

        if (entry == 0) {
            sclp_print("\n");
        }
    }

    sclp_print("\n");
    return get_boot_index(valid_entries);
}

int menu_get_enum_boot_index(bool *valid_entries)
{
    char tmp[3];
    int i;

    sclp_print("s390-ccw Enumerated Boot Menu.\n\n");

    for (i = 0; i < MAX_BOOT_ENTRIES; i++) {
        if (valid_entries[i]) {
            if (i < 10) {
                sclp_print(" ");
            }
            sclp_print("[");
            sclp_print(uitoa(i, tmp, sizeof(tmp)));
            sclp_print("]");
            if (i == 0) {
                sclp_print(" default\n");
            }
            sclp_print("\n");
        }
    }

    sclp_print("\n");
    return get_boot_index(valid_entries);
}

void menu_set_parms(uint8_t boot_menu_flag, uint32_t boot_menu_timeout)
{
    flag = boot_menu_flag;
    timeout = boot_menu_timeout;
}

bool menu_is_enabled_zipl(void)
{
    return flag & (QIPL_FLAG_BM_OPTS_CMD | QIPL_FLAG_BM_OPTS_ZIPL);
}

bool menu_is_enabled_enum(void)
{
    return flag & QIPL_FLAG_BM_OPTS_CMD;
}
