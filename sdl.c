/*
 * QEMU SDL display driver
 * 
 * Copyright (c) 2003 Fabrice Bellard
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

#include <SDL.h>

#ifndef _WIN32
#include <signal.h>
#endif

#if defined(__APPLE__)
#define CONFIG_SDL_GENERIC_KBD
#endif

static SDL_Surface *screen;
static int gui_grab; /* if true, all keyboard/mouse events are grabbed */
static int last_vm_running;
static int gui_saved_grab;
static int gui_fullscreen;
static int gui_key_modifier_pressed;
static int gui_keysym;

static void sdl_update(DisplayState *ds, int x, int y, int w, int h)
{
    //    printf("updating x=%d y=%d w=%d h=%d\n", x, y, w, h);
    SDL_UpdateRect(screen, x, y, w, h);
}

static void sdl_resize(DisplayState *ds, int w, int h)
{
    int flags;

    //    printf("resizing to %d %d\n", w, h);

    flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL;
    flags |= SDL_RESIZABLE;
    if (gui_fullscreen)
        flags |= SDL_FULLSCREEN;
    screen = SDL_SetVideoMode(w, h, 0, flags);
    if (!screen) {
        fprintf(stderr, "Could not open SDL display\n");
        exit(1);
    }
    ds->data = screen->pixels;
    ds->linesize = screen->pitch;
    ds->depth = screen->format->BitsPerPixel;
}

#ifdef CONFIG_SDL_GENERIC_KBD

/* XXX: use keymap tables defined in the VNC patch because the
   following code suppose you have a US keyboard. */

static const uint8_t scancodes[SDLK_LAST] = {
    [SDLK_ESCAPE]   = 0x01,
    [SDLK_1]        = 0x02,
    [SDLK_2]        = 0x03,
    [SDLK_3]        = 0x04,
    [SDLK_4]        = 0x05,
    [SDLK_5]        = 0x06,
    [SDLK_6]        = 0x07,
    [SDLK_7]        = 0x08,
    [SDLK_8]        = 0x09,
    [SDLK_9]        = 0x0a,
    [SDLK_0]        = 0x0b,
    [SDLK_MINUS]    = 0x0c,
    [SDLK_EQUALS]   = 0x0d,
    [SDLK_BACKSPACE]        = 0x0e,
    [SDLK_TAB]      = 0x0f,
    [SDLK_q]        = 0x10,
    [SDLK_w]        = 0x11,
    [SDLK_e]        = 0x12,
    [SDLK_r]        = 0x13,
    [SDLK_t]        = 0x14,
    [SDLK_y]        = 0x15,
    [SDLK_u]        = 0x16,
    [SDLK_i]        = 0x17,
    [SDLK_o]        = 0x18,
    [SDLK_p]        = 0x19,
    [SDLK_LEFTBRACKET]      = 0x1a,
    [SDLK_RIGHTBRACKET]     = 0x1b,
    [SDLK_RETURN]   = 0x1c,
    [SDLK_LCTRL]    = 0x1d,
    [SDLK_a]        = 0x1e,
    [SDLK_s]        = 0x1f,
    [SDLK_d]        = 0x20,
    [SDLK_f]        = 0x21,
    [SDLK_g]        = 0x22,
    [SDLK_h]        = 0x23,
    [SDLK_j]        = 0x24,
    [SDLK_k]        = 0x25,
    [SDLK_l]        = 0x26,
    [SDLK_SEMICOLON]        = 0x27,
    [SDLK_QUOTE]    = 0x28,
    [SDLK_BACKQUOTE]        = 0x29,
    [SDLK_LSHIFT]   = 0x2a,
    [SDLK_BACKSLASH]        = 0x2b,
    [SDLK_z]        = 0x2c,
    [SDLK_x]        = 0x2d,
    [SDLK_c]        = 0x2e,
    [SDLK_v]        = 0x2f,
    [SDLK_b]        = 0x30,
    [SDLK_n]        = 0x31,
    [SDLK_m]        = 0x32,
    [SDLK_COMMA]    = 0x33,
    [SDLK_PERIOD]   = 0x34,
    [SDLK_SLASH]    = 0x35,
    [SDLK_KP_MULTIPLY]      = 0x37,
    [SDLK_LALT]     = 0x38,
    [SDLK_SPACE]    = 0x39,
    [SDLK_CAPSLOCK] = 0x3a,
    [SDLK_F1]       = 0x3b,
    [SDLK_F2]       = 0x3c,
    [SDLK_F3]       = 0x3d,
    [SDLK_F4]       = 0x3e,
    [SDLK_F5]       = 0x3f,
    [SDLK_F6]       = 0x40,
    [SDLK_F7]       = 0x41,
    [SDLK_F8]       = 0x42,
    [SDLK_F9]       = 0x43,
    [SDLK_F10]      = 0x44,
    [SDLK_NUMLOCK]  = 0x45,
    [SDLK_SCROLLOCK]        = 0x46,
    [SDLK_KP7]      = 0x47,
    [SDLK_KP8]      = 0x48,
    [SDLK_KP9]      = 0x49,
    [SDLK_KP_MINUS] = 0x4a,
    [SDLK_KP4]      = 0x4b,
    [SDLK_KP5]      = 0x4c,
    [SDLK_KP6]      = 0x4d,
    [SDLK_KP_PLUS]  = 0x4e,
    [SDLK_KP1]      = 0x4f,
    [SDLK_KP2]      = 0x50,
    [SDLK_KP3]      = 0x51,
    [SDLK_KP0]      = 0x52,
    [SDLK_KP_PERIOD]        = 0x53,
    [SDLK_PRINT]    = 0x54,
    [SDLK_LMETA]    = 0x56,

    [SDLK_KP_ENTER]  = 0x9c,
    [SDLK_KP_DIVIDE] = 0xb5,
    
    [SDLK_UP]       = 0xc8,
    [SDLK_DOWN]     = 0xd0,
    [SDLK_RIGHT]    = 0xcd,
    [SDLK_LEFT]     = 0xcb,
    [SDLK_INSERT]   = 0xd2,
    [SDLK_HOME]     = 0xc7,
    [SDLK_END]      = 0xcf,
    [SDLK_PAGEUP]   = 0xc9,
    [SDLK_PAGEDOWN] = 0xd1,
    [SDLK_DELETE]   = 0xd3,
};

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    return scancodes[ev->keysym.sym];
}

#elif defined(_WIN32)

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    return ev->keysym.scancode;
}

#else

static const uint8_t x_keycode_to_pc_keycode[61] = {
   0xc7,      /*  97  Home   */
   0xc8,      /*  98  Up     */
   0xc9,      /*  99  PgUp   */
   0xcb,      /* 100  Left   */
   0x4c,        /* 101  KP-5   */
   0xcd,      /* 102  Right  */
   0xcf,      /* 103  End    */
   0xd0,      /* 104  Down   */
   0xd1,      /* 105  PgDn   */
   0xd2,      /* 106  Ins    */
   0xd3,      /* 107  Del    */
   0x9c,      /* 108  Enter  */
   0x9d,      /* 109  Ctrl-R */
   0x0,       /* 110  Pause  */
   0xb7,      /* 111  Print  */
   0xb5,      /* 112  Divide */
   0xb8,      /* 113  Alt-R  */
   0xc6,      /* 114  Break  */   
   0x0,         /* 115 */
   0x0,         /* 116 */
   0x0,         /* 117 */
   0x0,         /* 118 */
   0x0,         /* 119 */
   0x70,         /* 120 Hiragana_Katakana */
   0x0,         /* 121 */
   0x0,         /* 122 */
   0x73,         /* 123 backslash */
   0x0,         /* 124 */
   0x0,         /* 125 */
   0x0,         /* 126 */
   0x0,         /* 127 */
   0x0,         /* 128 */
   0x79,         /* 129 Henkan */
   0x0,         /* 130 */
   0x7b,         /* 131 Muhenkan */
   0x0,         /* 132 */
   0x7d,         /* 133 Yen */
   0x0,         /* 134 */
   0x0,         /* 135 */
   0x47,         /* 136 KP_7 */
   0x48,         /* 137 KP_8 */
   0x49,         /* 138 KP_9 */
   0x4b,         /* 139 KP_4 */
   0x4c,         /* 140 KP_5 */
   0x4d,         /* 141 KP_6 */
   0x4f,         /* 142 KP_1 */
   0x50,         /* 143 KP_2 */
   0x51,         /* 144 KP_3 */
   0x52,         /* 145 KP_0 */
   0x53,         /* 146 KP_. */
   0x47,         /* 147 KP_HOME */
   0x48,         /* 148 KP_UP */
   0x49,         /* 149 KP_PgUp */
   0x4b,         /* 150 KP_Left */
   0x4c,         /* 151 KP_ */
   0x4d,         /* 152 KP_Right */
   0x4f,         /* 153 KP_End */
   0x50,         /* 154 KP_Down */
   0x51,         /* 155 KP_PgDn */
   0x52,         /* 156 KP_Ins */
   0x53,         /* 157 KP_Del */
};

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    int keycode;

    keycode = ev->keysym.scancode;

    if (keycode < 9) {
        keycode = 0;
    } else if (keycode < 97) {
        keycode -= 8; /* just an offset */
    } else if (keycode < 158) {
        /* use conversion table */
        keycode = x_keycode_to_pc_keycode[keycode - 97];
    } else {
        keycode = 0;
    }
    return keycode;
}

#endif

static void sdl_process_key(SDL_KeyboardEvent *ev)
{
    int keycode, v, i;
    static uint8_t modifiers_state[256];

    if (ev->keysym.sym == SDLK_PAUSE) {
        /* specific case */
        v = 0;
        if (ev->type == SDL_KEYUP)
            v |= 0x80;
        kbd_put_keycode(0xe1);
        kbd_put_keycode(0x1d | v);
        kbd_put_keycode(0x45 | v);
        return;
    }

    /* XXX: not portable, but avoids complicated mappings */
    keycode = sdl_keyevent_to_keycode(ev);

    switch(keycode) {
    case 0x00:
        /* sent when leaving window: reset the modifiers state */
        for(i = 0; i < 256; i++) {
            if (modifiers_state[i]) {
                if (i & 0x80)
                    kbd_put_keycode(0xe0);
                kbd_put_keycode(i | 0x80);
            }
        }
        return;
    case 0x2a:                          /* Left Shift */
    case 0x36:                          /* Right Shift */
    case 0x1d:                          /* Left CTRL */
    case 0x9d:                          /* Right CTRL */
    case 0x38:                          /* Left ALT */
    case 0xb8:                         /* Right ALT */
        if (ev->type == SDL_KEYUP)
            modifiers_state[keycode] = 0;
        else
            modifiers_state[keycode] = 1;
        break;
    case 0x45: /* num lock */
    case 0x3a: /* caps lock */
        /* SDL does not send the key up event, so we generate it */
        kbd_put_keycode(keycode);
        kbd_put_keycode(keycode | 0x80);
        return;
    }

    /* now send the key code */
    if (keycode & 0x80)
        kbd_put_keycode(0xe0);
    if (ev->type == SDL_KEYUP)
        kbd_put_keycode(keycode | 0x80);
    else
        kbd_put_keycode(keycode & 0x7f);
}

static void sdl_update_caption(void)
{
    char buf[1024];
    strcpy(buf, "QEMU");
    if (!vm_running) {
        strcat(buf, " [Stopped]");
    }
    if (gui_grab) {
        strcat(buf, " - Press Ctrl-Shift to exit grab");
    }
    SDL_WM_SetCaption(buf, "QEMU");
}

static void sdl_grab_start(void)
{
    SDL_ShowCursor(0);
    SDL_WM_GrabInput(SDL_GRAB_ON);
    /* dummy read to avoid moving the mouse */
    SDL_GetRelativeMouseState(NULL, NULL);
    gui_grab = 1;
    sdl_update_caption();
}

static void sdl_grab_end(void)
{
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_ShowCursor(1);
    gui_grab = 0;
    sdl_update_caption();
}

static void sdl_send_mouse_event(void)
{
    int dx, dy, dz, state, buttons;
    state = SDL_GetRelativeMouseState(&dx, &dy);
    buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
        buttons |= MOUSE_EVENT_LBUTTON;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
        buttons |= MOUSE_EVENT_RBUTTON;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
        buttons |= MOUSE_EVENT_MBUTTON;
    /* XXX: test wheel */
    dz = 0;
#ifdef SDL_BUTTON_WHEELUP
    if (state & SDL_BUTTON(SDL_BUTTON_WHEELUP))
        dz--;
    if (state & SDL_BUTTON(SDL_BUTTON_WHEELDOWN))
        dz++;
#endif
    kbd_mouse_event(dx, dy, dz, buttons);
}

static void toggle_full_screen(DisplayState *ds)
{
    gui_fullscreen = !gui_fullscreen;
    sdl_resize(ds, screen->w, screen->h);
    if (gui_fullscreen) {
        gui_saved_grab = gui_grab;
        sdl_grab_start();
    } else {
        if (!gui_saved_grab)
            sdl_grab_end();
    }
    vga_invalidate_display();
    vga_update_display();
}

static void sdl_refresh(DisplayState *ds)
{
    SDL_Event ev1, *ev = &ev1;
    int mod_state;
                     
    if (last_vm_running != vm_running) {
        last_vm_running = vm_running;
        sdl_update_caption();
    }

    vga_update_display();
    while (SDL_PollEvent(ev)) {
        switch (ev->type) {
        case SDL_VIDEOEXPOSE:
            sdl_update(ds, 0, 0, screen->w, screen->h);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (ev->type == SDL_KEYDOWN) {
                mod_state = (SDL_GetModState() & (KMOD_LSHIFT | KMOD_LCTRL)) ==
                    (KMOD_LSHIFT | KMOD_LCTRL);
                gui_key_modifier_pressed = mod_state;
                if (gui_key_modifier_pressed && 
                    ev->key.keysym.sym == SDLK_f) {
                    gui_keysym = ev->key.keysym.sym;
                }
            } else if (ev->type == SDL_KEYUP) {
                mod_state = (SDL_GetModState() & (KMOD_LSHIFT | KMOD_LCTRL));
                if (!mod_state) {
                    if (gui_key_modifier_pressed) {
                        switch(gui_keysym) {
                        case SDLK_f:
                            toggle_full_screen(ds);
                            break;
                        case 0:
                            /* exit/enter grab if pressing Ctrl-Shift */
                            if (!gui_grab)
                                sdl_grab_start();
                            else
                                sdl_grab_end();
                            break;
                        }
                        gui_key_modifier_pressed = 0;
                        gui_keysym = 0;
                    }
                }
            }
            sdl_process_key(&ev->key);
            break;
        case SDL_QUIT:
            qemu_system_shutdown_request();
            break;
        case SDL_MOUSEMOTION:
            if (gui_grab) {
                sdl_send_mouse_event();
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            {
                SDL_MouseButtonEvent *bev = &ev->button;
                if (!gui_grab) {
                    if (ev->type == SDL_MOUSEBUTTONDOWN &&
                        (bev->state & SDL_BUTTON_LMASK)) {
                        /* start grabbing all events */
                        sdl_grab_start();
                    }
                } else {
                    sdl_send_mouse_event();
                }
            }
            break;
        case SDL_ACTIVEEVENT:
            if (gui_grab && (ev->active.gain & SDL_ACTIVEEVENTMASK) == 0) {
                sdl_grab_end();
            }
            break;
        default:
            break;
        }
    }
}

static void sdl_cleanup(void) 
{
    SDL_Quit();
}

void sdl_display_init(DisplayState *ds)
{
    int flags;

    flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL - exiting\n");
        exit(1);
    }

#ifndef _WIN32
    /* NOTE: we still want Ctrl-C to work, so we undo the SDL redirections */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
#endif

    ds->dpy_update = sdl_update;
    ds->dpy_resize = sdl_resize;
    ds->dpy_refresh = sdl_refresh;

    sdl_resize(ds, 640, 400);
    sdl_update_caption();
    SDL_EnableKeyRepeat(250, 50);
    gui_grab = 0;

    atexit(sdl_cleanup);
}
