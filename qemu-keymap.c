/*
 * QEMU keymap utility
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Authors:
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/notify.h"
#include "ui/input.h"

#include <xkbcommon/xkbcommon.h>

struct xkb_rule_names names = {
    .rules   = NULL,
    .model   = "pc105",
    .layout  = "us",
    .variant = NULL,
    .options = NULL,
};

static xkb_mod_mask_t shift;
static xkb_mod_mask_t ctrl;
static xkb_mod_mask_t altgr;
static xkb_mod_mask_t numlock;

static FILE *outfile;

/* ------------------------------------------------------------------------ */

static uint32_t qcode_to_number(uint32_t qcode)
{
    KeyValue keyvalue;
    uint32_t number;

    keyvalue.type = KEY_VALUE_KIND_QCODE;
    keyvalue.u.qcode.data = qcode;
    number = qemu_input_key_value_to_number(&keyvalue);
    assert(number != 0);
    return number;
}

static void print_sym(xkb_keysym_t sym, uint32_t qcode, const char *mod)
{
    char name[64];

    if (sym == XKB_KEY_NoSymbol) {
        return;
    }
    xkb_keysym_get_name(sym, name, sizeof(name));

    /* TODO: make ui/keymap.c parser accept QKeyCode names */
    fprintf(outfile, "%s 0x%02x%s\n", name, qcode_to_number(qcode), mod);
}

static void walk_map(struct xkb_keymap *map, xkb_keycode_t code, void *data)
{
    struct xkb_state *state = data;
    xkb_keysym_t kbase, knumlock, kshift, kaltgr, kaltgrshift;
    uint32_t evdev, qcode;
    char name[64];

    fprintf(outfile, "\n");

    /*
     * map xkb keycode -> QKeyCode
     *
     * xkb keycode is linux evdev shifted by 8
     */
    evdev = code - 8;
    qcode = qemu_input_linux_to_qcode(evdev);
    if (qcode == Q_KEY_CODE_UNMAPPED) {
        xkb_state_update_mask(state,  0, 0, 0,  0, 0, 0);
        kbase = xkb_state_key_get_one_sym(state, code);
        xkb_keysym_get_name(kbase, name, sizeof(name));
        fprintf(outfile, "# evdev %d (0x%x): no evdev -> QKeyCode mapping"
                " (xkb keysym %s)\n", evdev, evdev, name);
        return;
    }
    fprintf(outfile, "# evdev %d (0x%x), QKeyCode \"%s\", number 0x%x\n",
            evdev, evdev,
            QKeyCode_str(qcode),
            qcode_to_number(qcode));

    /*
     * check which modifier states generate which keysyms
     */
    xkb_state_update_mask(state,  0, 0, 0,  0, 0, 0);
    kbase = xkb_state_key_get_one_sym(state, code);
    print_sym(kbase, qcode, "");

    xkb_state_update_mask(state,  0, 0, numlock,  0, 0, 0);
    knumlock = xkb_state_key_get_one_sym(state, code);
    if (kbase != knumlock) {
        print_sym(knumlock, qcode, " numlock");
    }

    xkb_state_update_mask(state,  shift, 0, 0,  0, 0, 0);
    kshift = xkb_state_key_get_one_sym(state, code);
    if (kbase != kshift && knumlock != kshift) {
        print_sym(kshift, qcode, " shift");
    }

    xkb_state_update_mask(state,  altgr, 0, 0,  0, 0, 0);
    kaltgr = xkb_state_key_get_one_sym(state, code);
    if (kbase != kaltgr) {
        print_sym(kaltgr, qcode, " altgr");
    }

    xkb_state_update_mask(state,  altgr | shift, 0, 0,  0, 0, 0);
    kaltgrshift = xkb_state_key_get_one_sym(state, code);
    if (kshift != kaltgrshift && kaltgr != kaltgrshift) {
        print_sym(kaltgrshift, qcode, " shift altgr");
    }
    return;
}

static void usage(FILE *out)
{
    fprintf(out,
            "\n"
            "This tool generates qemu reverse keymaps from xkb keymaps,\n"
            "which can be used with the qemu \"-k\" command line switch.\n"
            "\n"
            "usage: qemu-keymap <options>\n"
            "options:\n"
            "    -h             print this text\n"
            "    -f <file>      set output file          (default: stdout)\n"
            "    -m <model>     set kbd model            (default: %s)\n"
            "    -l <layout>    set kbd layout           (default: %s)\n"
            "    -v <variant>   set kbd variant          (default: %s)\n"
            "    -o <options>   set kbd options          (default: %s)\n"
            "\n",
            names.model, names.layout,
            names.variant ?: "-",
            names.options ?: "-");
}

int main(int argc, char *argv[])
{
    struct xkb_context *ctx;
    struct xkb_keymap *map;
    struct xkb_state *state;
    xkb_mod_index_t mod, mods;
    int rc;

    for (;;) {
        rc = getopt(argc, argv, "hm:l:v:o:f:");
        if (rc == -1) {
            break;
        }
        switch (rc) {
        case 'm':
            names.model = optarg;
            break;
        case 'l':
            names.layout = optarg;
            break;
        case 'v':
            names.variant = optarg;
            break;
        case 'o':
            names.options = optarg;
            break;
        case 'f':
            outfile = fopen(optarg, "w");
            if (outfile == NULL) {
                fprintf(stderr, "open %s: %s\n", optarg, strerror(errno));
                exit(1);
            }
            break;
        case 'h':
            usage(stdout);
            exit(0);
        default:
            usage(stderr);
            exit(1);
        }
    }

    if (outfile == NULL) {
        outfile = stdout;
    }

    fprintf(outfile,
            "# SPDX-License-Identifier: GPL-2.0-or-later\n"
            "#\n"
            "# generated by qemu-keymap\n"
            "#    model   : %s\n"
            "#    layout  : %s\n"
            "#    variant : %s\n"
            "#    options : %s\n"
            "\n",
            names.model, names.layout,
            names.variant ?: "-",
            names.options ?: "-");

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!map) {
        /* libxkbcommon prints error */
        exit(1);
    }

    fprintf(outfile, "# name: \"%s\"\n\n",
            xkb_keymap_layout_get_name(map, 0));
    fprintf(outfile, "# modifiers\n");
    mods = xkb_keymap_num_mods(map);
    for (mod = 0; mod < mods; mod++) {
        fprintf(outfile, "#    %2d: %s\n",
                mod, xkb_keymap_mod_get_name(map, mod));
    }

    mod = xkb_keymap_mod_get_index(map, "Shift");
    shift = (1 << mod);
    mod = xkb_keymap_mod_get_index(map, "Control");
    ctrl = (1 << mod);
    mod = xkb_keymap_mod_get_index(map, "AltGr");
    altgr = (1 << mod);
    mod = xkb_keymap_mod_get_index(map, "NumLock");
    numlock = (1 << mod);

    state = xkb_state_new(map);
    xkb_keymap_key_for_each(map, walk_map, state);

    /* add quirks */
    fprintf(outfile,
            "\n"
            "#\n"
            "# quirks section start\n"
            "#\n"
            "# Sometimes multiple keysyms map to the same keycodes.\n"
            "# The keycode -> keysym lookup finds only one of the\n"
            "# keysyms.  So append them here.\n"
            "#\n"
            "\n");
    print_sym(XKB_KEY_Print,            Q_KEY_CODE_SYSRQ,      "");
    print_sym(XKB_KEY_Sys_Req,          Q_KEY_CODE_SYSRQ,      "");
    print_sym(XKB_KEY_Execute,          Q_KEY_CODE_SYSRQ,      "");

    print_sym(XKB_KEY_KP_Decimal,       Q_KEY_CODE_KP_DECIMAL, " numlock");
    print_sym(XKB_KEY_KP_Separator,     Q_KEY_CODE_KP_DECIMAL, " numlock");

    print_sym(XKB_KEY_Alt_R,            Q_KEY_CODE_ALT_R,      "");
    print_sym(XKB_KEY_ISO_Level3_Shift, Q_KEY_CODE_ALT_R,      "");
    print_sym(XKB_KEY_Mode_switch,      Q_KEY_CODE_ALT_R,      "");

    fprintf(outfile,
            "\n"
            "# quirks section end\n");

    exit(0);
}
