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
#include "keymaps.h"
#include "sysemu/sysemu.h"

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


static void add_to_key_range(struct key_range **krp, int code) {
    struct key_range *kr;
    for (kr = *krp; kr; kr = kr->next) {
        if (code >= kr->start && code <= kr->end) {
            break;
        }
        if (code == kr->start - 1) {
            kr->start--;
            break;
        }
        if (code == kr->end + 1) {
            kr->end++;
            break;
        }
    }
    if (kr == NULL) {
        kr = g_malloc0(sizeof(*kr));
        kr->start = kr->end = code;
        kr->next = *krp;
        *krp = kr;
    }
}

static void add_keysym(char *line, int keysym, int keycode, kbd_layout_t *k) {
    if (keysym < MAX_NORMAL_KEYCODE) {
        /* fprintf(stderr,"Setting keysym %s (%d) to %d\n",
                   line, keysym, keycode); */
        k->keysym2keycode[keysym] = keycode;
    } else {
        if (k->extra_count >= MAX_EXTRA_COUNT) {
            fprintf(stderr, "Warning: Could not assign keysym %s (0x%x)"
                    " because of memory constraints.\n", line, keysym);
        } else {
#if 0
            fprintf(stderr, "Setting %d: %d,%d\n",
                    k->extra_count, keysym, keycode);
#endif
            k->keysym2keycode_extra[k->extra_count].
            keysym = keysym;
            k->keysym2keycode_extra[k->extra_count].
            keycode = keycode;
            k->extra_count++;
        }
    }
}

static kbd_layout_t *parse_keyboard_layout(const name2keysym_t *table,
                                           const char *language,
                                           kbd_layout_t *k)
{
    FILE *f;
    char * filename;
    char line[1024];
    int len;

    filename = qemu_find_file(QEMU_FILE_TYPE_KEYMAP, language);
    f = filename ? fopen(filename, "r") : NULL;
    g_free(filename);
    if (!f) {
        fprintf(stderr, "Could not read keymap file: '%s'\n", language);
        return NULL;
    }

    if (!k) {
        k = g_new0(kbd_layout_t, 1);
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
            parse_keyboard_layout(table, line + 8, k);
        } else {
            char *end_of_keysym = line;
            while (*end_of_keysym != 0 && *end_of_keysym != ' ') {
                end_of_keysym++;
            }
            if (*end_of_keysym) {
                int keysym;
                *end_of_keysym = 0;
                keysym = get_keysym(table, line);
                if (keysym == 0) {
                    /* fprintf(stderr, "Warning: unknown keysym %s\n", line);*/
                } else {
                    const char *rest = end_of_keysym + 1;
                    int keycode = strtol(rest, NULL, 0);

                    if (strstr(rest, "numlock")) {
                        add_to_key_range(&k->keypad_range, keycode);
                        add_to_key_range(&k->numlock_range, keysym);
                        /* fprintf(stderr, "keypad keysym %04x keycode %d\n",
                                   keysym, keycode); */
                    }

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
                        for (c = line; *c; c++) {
                            *c = qemu_toupper(*c);
                        }
                        keysym = get_keysym(table, line);
                        if (keysym) {
                            add_keysym(line, keysym,
                                       keycode | SCANCODE_SHIFT, k);
                        }
                    }
                }
            }
        }
    }
    fclose(f);
    return k;
}


void *init_keyboard_layout(const name2keysym_t *table, const char *language)
{
    return parse_keyboard_layout(table, language, NULL);
}


int keysym2scancode(void *kbd_layout, int keysym)
{
    kbd_layout_t *k = kbd_layout;
    if (keysym < MAX_NORMAL_KEYCODE) {
        if (k->keysym2keycode[keysym] == 0) {
            fprintf(stderr, "Warning: no scancode found for keysym %d\n",
                    keysym);
        }
        return k->keysym2keycode[keysym];
    } else {
        int i;
#ifdef XK_ISO_Left_Tab
        if (keysym == XK_ISO_Left_Tab) {
            keysym = XK_Tab;
        }
#endif
        for (i = 0; i < k->extra_count; i++) {
            if (k->keysym2keycode_extra[i].keysym == keysym) {
                return k->keysym2keycode_extra[i].keycode;
            }
        }
    }
    return 0;
}

int keycode_is_keypad(void *kbd_layout, int keycode)
{
    kbd_layout_t *k = kbd_layout;
    struct key_range *kr;

    for (kr = k->keypad_range; kr; kr = kr->next) {
        if (keycode >= kr->start && keycode <= kr->end) {
            return 1;
        }
    }
    return 0;
}

int keysym_is_numlock(void *kbd_layout, int keysym)
{
    kbd_layout_t *k = kbd_layout;
    struct key_range *kr;

    for (kr = k->numlock_range; kr; kr = kr->next) {
        if (keysym >= kr->start && keysym <= kr->end) {
            return 1;
        }
    }
    return 0;
}
