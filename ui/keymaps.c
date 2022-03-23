/*
 * QEMU keysym to keycode conversion using rdesktop keymaps
 *
 * Copyright (c) 2004 Johannes Schindelin
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

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "keymaps.h"
#include "trace.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "ui/input.h"

struct keysym2code {
    uint32_t count;
    uint16_t keycodes[4];
};

struct kbd_layout_t {
    GHashTable *hash;
};

static int get_keysym(const name2keysym_t *table,
                      const char *name)
{
    const name2keysym_t *p;
    for(p = table; p->name != NULL; p++) {
        if (!strcmp(p->name, name)) {
            return p->keysym;
        }
    }
    if (name[0] == 'U' && strlen(name) == 5) { /* try unicode Uxxxx */
        char *end;
        int ret = (int)strtoul(name + 1, &end, 16);
        if (*end == '\0' && ret > 0) {
            return ret;
        }
    }
    return 0;
}


static void add_keysym(char *line, int keysym, int keycode, kbd_layout_t *k)
{
    struct keysym2code *keysym2code;

    keysym2code = g_hash_table_lookup(k->hash, GINT_TO_POINTER(keysym));
    if (keysym2code) {
        if (keysym2code->count < ARRAY_SIZE(keysym2code->keycodes)) {
            keysym2code->keycodes[keysym2code->count++] = keycode;
        } else {
            warn_report("more than %zd keycodes for keysym %d",
                        ARRAY_SIZE(keysym2code->keycodes), keysym);
        }
        return;
    }

    keysym2code = g_new0(struct keysym2code, 1);
    keysym2code->keycodes[0] = keycode;
    keysym2code->count = 1;
    g_hash_table_replace(k->hash, GINT_TO_POINTER(keysym), keysym2code);
    trace_keymap_add(keysym, keycode, line);
}

static int parse_keyboard_layout(kbd_layout_t *k,
                                 const name2keysym_t *table,
                                 const char *language, Error **errp)
{
    int ret;
    FILE *f;
    char * filename;
    char line[1024];
    char keyname[64];
    int len;

    filename = qemu_find_file(QEMU_FILE_TYPE_KEYMAP, language);
    trace_keymap_parse(filename);
    f = filename ? fopen(filename, "r") : NULL;
    g_free(filename);
    if (!f) {
        error_setg(errp, "could not read keymap file: '%s'", language);
        return -1;
    }

    for(;;) {
        if (fgets(line, 1024, f) == NULL) {
            break;
        }
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        if (line[0] == '#') {
            continue;
        }
        if (!strncmp(line, "map ", 4)) {
            continue;
        }
        if (!strncmp(line, "include ", 8)) {
            error_setg(errp, "keymap include files are not supported any more");
            ret = -1;
            goto out;
        } else {
            int offset = 0;
            while (line[offset] != 0 &&
                   line[offset] != ' ' &&
                   offset < sizeof(keyname) - 1) {
                keyname[offset] = line[offset];
                offset++;
            }
            keyname[offset] = 0;
            if (strlen(keyname)) {
                int keysym;
                keysym = get_keysym(table, keyname);
                if (keysym == 0) {
                    /* warn_report("unknown keysym %s", line);*/
                } else {
                    const char *rest = line + offset + 1;
                    int keycode = strtol(rest, NULL, 0);

                    if (strstr(rest, "shift")) {
                        keycode |= SCANCODE_SHIFT;
                    }
                    if (strstr(rest, "altgr")) {
                        keycode |= SCANCODE_ALTGR;
                    }
                    if (strstr(rest, "ctrl")) {
                        keycode |= SCANCODE_CTRL;
                    }

                    add_keysym(line, keysym, keycode, k);

                    if (strstr(rest, "addupper")) {
                        char *c;
                        for (c = keyname; *c; c++) {
                            *c = qemu_toupper(*c);
                        }
                        keysym = get_keysym(table, keyname);
                        if (keysym) {
                            add_keysym(line, keysym,
                                       keycode | SCANCODE_SHIFT, k);
                        }
                    }
                }
            }
        }
    }

    ret = 0;
out:
    fclose(f);
    return ret;
}


kbd_layout_t *init_keyboard_layout(const name2keysym_t *table,
                                   const char *language, Error **errp)
{
    kbd_layout_t *k;

    k = g_new0(kbd_layout_t, 1);
    k->hash = g_hash_table_new(NULL, NULL);
    if (parse_keyboard_layout(k, table, language, errp) < 0) {
        g_hash_table_unref(k->hash);
        g_free(k);
        return NULL;
    }
    return k;
}


int keysym2scancode(kbd_layout_t *k, int keysym,
                    QKbdState *kbd, bool down)
{
    static const uint32_t mask =
        SCANCODE_SHIFT | SCANCODE_ALTGR | SCANCODE_CTRL;
    uint32_t mods, i;
    struct keysym2code *keysym2code;

#ifdef XK_ISO_Left_Tab
    if (keysym == XK_ISO_Left_Tab) {
        keysym = XK_Tab;
    }
#endif

    keysym2code = g_hash_table_lookup(k->hash, GINT_TO_POINTER(keysym));
    if (!keysym2code) {
        trace_keymap_unmapped(keysym);
        warn_report("no scancode found for keysym %d", keysym);
        return 0;
    }

    if (keysym2code->count == 1) {
        return keysym2code->keycodes[0];
    }

    /* We have multiple keysym -> keycode mappings. */
    if (down) {
        /*
         * On keydown: Check whenever we find one mapping where the
         * modifier state of the mapping matches the current user
         * interface modifier state.  If so, prefer that one.
         */
        mods = 0;
        if (kbd && qkbd_state_modifier_get(kbd, QKBD_MOD_SHIFT)) {
            mods |= SCANCODE_SHIFT;
        }
        if (kbd && qkbd_state_modifier_get(kbd, QKBD_MOD_ALTGR)) {
            mods |= SCANCODE_ALTGR;
        }
        if (kbd && qkbd_state_modifier_get(kbd, QKBD_MOD_CTRL)) {
            mods |= SCANCODE_CTRL;
        }

        for (i = 0; i < keysym2code->count; i++) {
            if ((keysym2code->keycodes[i] & mask) == mods) {
                return keysym2code->keycodes[i];
            }
        }
    } else {
        /*
         * On keyup: Try find a key which is actually down.
         */
        for (i = 0; i < keysym2code->count; i++) {
            QKeyCode qcode = qemu_input_key_number_to_qcode
                (keysym2code->keycodes[i]);
            if (kbd && qkbd_state_key_get(kbd, qcode)) {
                return keysym2code->keycodes[i];
            }
        }
    }
    return keysym2code->keycodes[0];
}

int keycode_is_keypad(kbd_layout_t *k, int keycode)
{
    if (keycode >= 0x47 && keycode <= 0x53) {
        return true;
    }
    return false;
}

int keysym_is_numlock(kbd_layout_t *k, int keysym)
{
    switch (keysym) {
    case 0xffb0 ... 0xffb9:  /* KP_0 .. KP_9 */
    case 0xffac:             /* KP_Separator */
    case 0xffae:             /* KP_Decimal   */
        return true;
    }
    return false;
}
