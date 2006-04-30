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

static int get_keysym(const char *name)
{
    name2keysym_t *p;
    for(p = name2keysym; p->name != NULL; p++) {
        if (!strcmp(p->name, name))
            return p->keysym;
    }
    return 0;
}

#define MAX_NORMAL_KEYCODE 512
#define MAX_EXTRA_COUNT 256
typedef struct {
    uint16_t keysym2keycode[MAX_NORMAL_KEYCODE];
    struct {
	int keysym;
	uint16_t keycode;
    } keysym2keycode_extra[MAX_EXTRA_COUNT];
    int extra_count;
} kbd_layout_t;

static kbd_layout_t *parse_keyboard_layout(const char *language,
					   kbd_layout_t * k)
{
    FILE *f;
    char file_name[1024];
    char line[1024];
    int len;

    snprintf(file_name, sizeof(file_name),
             "%s/keymaps/%s", bios_dir, language);

    if (!k)
	k = qemu_mallocz(sizeof(kbd_layout_t));
    if (!k)
        return 0;
    if (!(f = fopen(file_name, "r"))) {
	fprintf(stderr,
		"Could not read keymap file: '%s'\n", file_name);
	return 0;
    }
    for(;;) {
	if (fgets(line, 1024, f) == NULL)
            break;
        len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (line[0] == '#')
	    continue;
	if (!strncmp(line, "map ", 4))
	    continue;
	if (!strncmp(line, "include ", 8)) {
	    parse_keyboard_layout(line + 8, k);
        } else {
	    char *end_of_keysym = line;
	    while (*end_of_keysym != 0 && *end_of_keysym != ' ')
		end_of_keysym++;
	    if (*end_of_keysym) {
		int keysym;
		*end_of_keysym = 0;
		keysym = get_keysym(line);
		if (keysym == 0) {
                    //		    fprintf(stderr, "Warning: unknown keysym %s\n", line);
		} else {
		    const char *rest = end_of_keysym + 1;
		    int keycode = strtol(rest, NULL, 0);
		    /* if(keycode&0x80)
		       keycode=(keycode<<8)^0x80e0; */
		    if (keysym < MAX_NORMAL_KEYCODE) {
			//fprintf(stderr,"Setting keysym %s (%d) to %d\n",line,keysym,keycode);
			k->keysym2keycode[keysym] = keycode;
		    } else {
			if (k->extra_count >= MAX_EXTRA_COUNT) {
			    fprintf(stderr,
				    "Warning: Could not assign keysym %s (0x%x) because of memory constraints.\n",
				    line, keysym);
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
	    }
	}
    }
    fclose(f);
    return k;
}

static void *init_keyboard_layout(const char *language)
{
    return parse_keyboard_layout(language, 0);
}

static int keysym2scancode(void *kbd_layout, int keysym)
{
    kbd_layout_t *k = kbd_layout;
    if (keysym < MAX_NORMAL_KEYCODE) {
	if (k->keysym2keycode[keysym] == 0)
	    fprintf(stderr, "Warning: no scancode found for keysym %d\n",
		    keysym);
	return k->keysym2keycode[keysym];
    } else {
	int i;
#ifdef XK_ISO_Left_Tab
	if (keysym == XK_ISO_Left_Tab)
	    keysym = XK_Tab;
#endif
	for (i = 0; i < k->extra_count; i++)
	    if (k->keysym2keycode_extra[i].keysym == keysym)
		return k->keysym2keycode_extra[i].keycode;
    }
    return 0;
}
