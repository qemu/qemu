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
#include "qemu-common.h"
#include "console.h"
#include "sysemu.h"

#include <SDL.h>

#ifndef _WIN32
#include <signal.h>
#endif

static DisplayChangeListener *dcl;
static SDL_Surface *real_screen;
static SDL_Surface *guest_screen = NULL;
static int gui_grab; /* if true, all keyboard/mouse events are grabbed */
static int last_vm_running;
static int gui_saved_grab;
static int gui_fullscreen;
static int gui_noframe;
static int gui_key_modifier_pressed;
static int gui_keysym;
static int gui_fullscreen_initial_grab;
static int gui_grab_code = KMOD_LALT | KMOD_LCTRL;
static uint8_t modifiers_state[256];
static int width, height;
static SDL_Cursor *sdl_cursor_normal;
static SDL_Cursor *sdl_cursor_hidden;
static int absolute_enabled = 0;
static int guest_cursor = 0;
static int guest_x, guest_y;
static SDL_Cursor *guest_sprite = 0;

static void sdl_update(DisplayState *ds, int x, int y, int w, int h)
{
    SDL_Rect rec;
    rec.x = x;
    rec.y = y;
    rec.w = w;
    rec.h = h;
    //    printf("updating x=%d y=%d w=%d h=%d\n", x, y, w, h);

    SDL_BlitSurface(guest_screen, &rec, real_screen, &rec);
    SDL_Flip(real_screen);
}

static void sdl_setdata(DisplayState *ds)
{
    SDL_Rect rec;
    rec.x = 0;
    rec.y = 0;
    rec.w = real_screen->w;
    rec.h = real_screen->h;

    if (guest_screen != NULL) SDL_FreeSurface(guest_screen);

    guest_screen = SDL_CreateRGBSurfaceFrom(ds_get_data(ds), ds_get_width(ds), ds_get_height(ds),
                                            ds_get_bits_per_pixel(ds), ds_get_linesize(ds),
                                            ds->surface->pf.rmask, ds->surface->pf.gmask,
                                            ds->surface->pf.bmask, ds->surface->pf.amask);
}

static void sdl_resize(DisplayState *ds)
{
    int flags;

    //    printf("resizing to %d %d\n", w, h);

    flags = SDL_HWSURFACE|SDL_ASYNCBLIT|SDL_HWACCEL;
    if (gui_fullscreen)
        flags |= SDL_FULLSCREEN;
    if (gui_noframe)
        flags |= SDL_NOFRAME;

    real_screen = SDL_SetVideoMode(ds_get_width(ds), ds_get_height(ds), 0, flags);
    if (!real_screen) {
        fprintf(stderr, "Could not open SDL display\n");
        exit(1);
    }

    sdl_setdata(ds);
}

/* generic keyboard conversion */

#include "sdl_keysym.h"
#include "keymaps.c"

static kbd_layout_t *kbd_layout = NULL;

static uint8_t sdl_keyevent_to_keycode_generic(const SDL_KeyboardEvent *ev)
{
    int keysym;
    /* workaround for X11+SDL bug with AltGR */
    keysym = ev->keysym.sym;
    if (keysym == 0 && ev->keysym.scancode == 113)
        keysym = SDLK_MODE;
    /* For Japanese key '\' and '|' */
    if (keysym == 92 && ev->keysym.scancode == 133) {
        keysym = 0xa5;
    }
    return keysym2scancode(kbd_layout, keysym);
}

/* specific keyboard conversions from scan codes */

#if defined(_WIN32)

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    return ev->keysym.scancode;
}

#else

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    int keycode;

    keycode = ev->keysym.scancode;

    if (keycode < 9) {
        keycode = 0;
    } else if (keycode < 97) {
        keycode -= 8; /* just an offset */
    } else if (keycode < 212) {
        /* use conversion table */
        keycode = _translate_keycode(keycode - 97);
    } else {
        keycode = 0;
    }
    return keycode;
}

#endif

static void reset_keys(void)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (modifiers_state[i]) {
            if (i & 0x80)
                kbd_put_keycode(0xe0);
            kbd_put_keycode(i | 0x80);
            modifiers_state[i] = 0;
        }
    }
}

static void sdl_process_key(SDL_KeyboardEvent *ev)
{
    int keycode, v;

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

    if (kbd_layout) {
        keycode = sdl_keyevent_to_keycode_generic(ev);
    } else {
        keycode = sdl_keyevent_to_keycode(ev);
    }

    switch(keycode) {
    case 0x00:
        /* sent when leaving window: reset the modifiers state */
        reset_keys();
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
    const char *status = "";

    if (!vm_running)
        status = " [Stopped]";
    else if (gui_grab) {
        if (!alt_grab)
            status = " - Press Ctrl-Alt to exit grab";
        else
            status = " - Press Ctrl-Alt-Shift to exit grab";
    }

    if (qemu_name)
        snprintf(buf, sizeof(buf), "QEMU (%s)%s", qemu_name, status);
    else
        snprintf(buf, sizeof(buf), "QEMU%s", status);

    SDL_WM_SetCaption(buf, "QEMU");
}

static void sdl_hide_cursor(void)
{
    if (!cursor_hide)
        return;

    if (kbd_mouse_is_absolute()) {
        SDL_ShowCursor(1);
        SDL_SetCursor(sdl_cursor_hidden);
    } else {
        SDL_ShowCursor(0);
    }
}

static void sdl_show_cursor(void)
{
    if (!cursor_hide)
        return;

    if (!kbd_mouse_is_absolute()) {
        SDL_ShowCursor(1);
        if (guest_cursor &&
                (gui_grab || kbd_mouse_is_absolute() || absolute_enabled))
            SDL_SetCursor(guest_sprite);
        else
            SDL_SetCursor(sdl_cursor_normal);
    }
}

static void sdl_grab_start(void)
{
    if (guest_cursor) {
        SDL_SetCursor(guest_sprite);
        SDL_WarpMouse(guest_x, guest_y);
    } else
        sdl_hide_cursor();

    if (SDL_WM_GrabInput(SDL_GRAB_ON) == SDL_GRAB_ON) {
        gui_grab = 1;
        sdl_update_caption();
    } else
        sdl_show_cursor();
}

static void sdl_grab_end(void)
{
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    gui_grab = 0;
    sdl_show_cursor();
    sdl_update_caption();
}

static void sdl_send_mouse_event(int dx, int dy, int dz, int x, int y, int state)
{
    int buttons;
    buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
        buttons |= MOUSE_EVENT_LBUTTON;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
        buttons |= MOUSE_EVENT_RBUTTON;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
        buttons |= MOUSE_EVENT_MBUTTON;

    if (kbd_mouse_is_absolute()) {
	if (!absolute_enabled) {
	    sdl_hide_cursor();
	    if (gui_grab) {
		sdl_grab_end();
	    }
	    absolute_enabled = 1;
	}

       dx = x * 0x7FFF / (width - 1);
       dy = y * 0x7FFF / (height - 1);
    } else if (absolute_enabled) {
	sdl_show_cursor();
	absolute_enabled = 0;
    } else if (guest_cursor) {
        x -= guest_x;
        y -= guest_y;
        guest_x += x;
        guest_y += y;
        dx = x;
        dy = y;
    }

    kbd_mouse_event(dx, dy, dz, buttons);
}

static void toggle_full_screen(DisplayState *ds)
{
    gui_fullscreen = !gui_fullscreen;
    sdl_resize(ds);
    if (gui_fullscreen) {
        gui_saved_grab = gui_grab;
        sdl_grab_start();
    } else {
        if (!gui_saved_grab)
            sdl_grab_end();
    }
    vga_hw_invalidate();
    vga_hw_update();
}

static void sdl_refresh(DisplayState *ds)
{
    SDL_Event ev1, *ev = &ev1;
    int mod_state;
    int buttonstate = SDL_GetMouseState(NULL, NULL);

    if (last_vm_running != vm_running) {
        last_vm_running = vm_running;
        sdl_update_caption();
    }

    vga_hw_update();
    SDL_EnableUNICODE(!is_graphic_console());

    while (SDL_PollEvent(ev)) {
        switch (ev->type) {
        case SDL_VIDEOEXPOSE:
            sdl_update(ds, 0, 0, real_screen->w, real_screen->h);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (ev->type == SDL_KEYDOWN) {
                if (!alt_grab) {
                    mod_state = (SDL_GetModState() & gui_grab_code) ==
                                gui_grab_code;
                } else {
                    mod_state = (SDL_GetModState() & (gui_grab_code | KMOD_LSHIFT)) ==
                                (gui_grab_code | KMOD_LSHIFT);
                }
                gui_key_modifier_pressed = mod_state;
                if (gui_key_modifier_pressed) {
                    int keycode;
                    keycode = sdl_keyevent_to_keycode(&ev->key);
                    switch(keycode) {
                    case 0x21: /* 'f' key on US keyboard */
                        toggle_full_screen(ds);
                        gui_keysym = 1;
                        break;
                    case 0x02 ... 0x0a: /* '1' to '9' keys */
                        /* Reset the modifiers sent to the current console */
                        reset_keys();
                        console_select(keycode - 0x02);
                        if (!is_graphic_console()) {
                            /* display grab if going to a text console */
                            if (gui_grab)
                                sdl_grab_end();
                        }
                        gui_keysym = 1;
                        break;
                    default:
                        break;
                    }
                } else if (!is_graphic_console()) {
                    int keysym;
                    keysym = 0;
                    if (ev->key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
                        switch(ev->key.keysym.sym) {
                        case SDLK_UP: keysym = QEMU_KEY_CTRL_UP; break;
                        case SDLK_DOWN: keysym = QEMU_KEY_CTRL_DOWN; break;
                        case SDLK_LEFT: keysym = QEMU_KEY_CTRL_LEFT; break;
                        case SDLK_RIGHT: keysym = QEMU_KEY_CTRL_RIGHT; break;
                        case SDLK_HOME: keysym = QEMU_KEY_CTRL_HOME; break;
                        case SDLK_END: keysym = QEMU_KEY_CTRL_END; break;
                        case SDLK_PAGEUP: keysym = QEMU_KEY_CTRL_PAGEUP; break;
                        case SDLK_PAGEDOWN: keysym = QEMU_KEY_CTRL_PAGEDOWN; break;
                        default: break;
                        }
                    } else {
                        switch(ev->key.keysym.sym) {
                        case SDLK_UP: keysym = QEMU_KEY_UP; break;
                        case SDLK_DOWN: keysym = QEMU_KEY_DOWN; break;
                        case SDLK_LEFT: keysym = QEMU_KEY_LEFT; break;
                        case SDLK_RIGHT: keysym = QEMU_KEY_RIGHT; break;
                        case SDLK_HOME: keysym = QEMU_KEY_HOME; break;
                        case SDLK_END: keysym = QEMU_KEY_END; break;
                        case SDLK_PAGEUP: keysym = QEMU_KEY_PAGEUP; break;
                        case SDLK_PAGEDOWN: keysym = QEMU_KEY_PAGEDOWN; break;
                        case SDLK_BACKSPACE: keysym = QEMU_KEY_BACKSPACE; break;
                        case SDLK_DELETE: keysym = QEMU_KEY_DELETE; break;
                        default: break;
                        }
                    }
                    if (keysym) {
                        kbd_put_keysym(keysym);
                    } else if (ev->key.keysym.unicode != 0) {
                        kbd_put_keysym(ev->key.keysym.unicode);
                    }
                }
            } else if (ev->type == SDL_KEYUP) {
                if (!alt_grab) {
                    mod_state = (ev->key.keysym.mod & gui_grab_code);
                } else {
                    mod_state = (ev->key.keysym.mod &
                                 (gui_grab_code | KMOD_LSHIFT));
                }
                if (!mod_state) {
                    if (gui_key_modifier_pressed) {
                        gui_key_modifier_pressed = 0;
                        if (gui_keysym == 0) {
                            /* exit/enter grab if pressing Ctrl-Alt */
                            if (!gui_grab) {
                                /* if the application is not active,
                                   do not try to enter grab state. It
                                   prevents
                                   'SDL_WM_GrabInput(SDL_GRAB_ON)'
                                   from blocking all the application
                                   (SDL bug). */
                                if (SDL_GetAppState() & SDL_APPACTIVE)
                                    sdl_grab_start();
                            } else {
                                sdl_grab_end();
                            }
                            /* SDL does not send back all the
                               modifiers key, so we must correct it */
                            reset_keys();
                            break;
                        }
                        gui_keysym = 0;
                    }
                }
            }
            if (is_graphic_console() && !gui_keysym)
                sdl_process_key(&ev->key);
            break;
        case SDL_QUIT:
            if (!no_quit)
                qemu_system_shutdown_request();
            break;
        case SDL_MOUSEMOTION:
            if (gui_grab || kbd_mouse_is_absolute() ||
                absolute_enabled) {
                sdl_send_mouse_event(ev->motion.xrel, ev->motion.yrel, 0,
                       ev->motion.x, ev->motion.y, ev->motion.state);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            {
                SDL_MouseButtonEvent *bev = &ev->button;
                if (!gui_grab && !kbd_mouse_is_absolute()) {
                    if (ev->type == SDL_MOUSEBUTTONDOWN &&
                        (bev->button == SDL_BUTTON_LEFT)) {
                        /* start grabbing all events */
                        sdl_grab_start();
                    }
                } else {
                    int dz;
                    dz = 0;
                    if (ev->type == SDL_MOUSEBUTTONDOWN) {
                        buttonstate |= SDL_BUTTON(bev->button);
                    } else {
                        buttonstate &= ~SDL_BUTTON(bev->button);
                    }
#ifdef SDL_BUTTON_WHEELUP
                    if (bev->button == SDL_BUTTON_WHEELUP && ev->type == SDL_MOUSEBUTTONDOWN) {
                        dz = -1;
                    } else if (bev->button == SDL_BUTTON_WHEELDOWN && ev->type == SDL_MOUSEBUTTONDOWN) {
                        dz = 1;
                    }
#endif
                    sdl_send_mouse_event(0, 0, dz, bev->x, bev->y, buttonstate);
                }
            }
            break;
        case SDL_ACTIVEEVENT:
            if (gui_grab && ev->active.state == SDL_APPINPUTFOCUS &&
                !ev->active.gain && !gui_fullscreen_initial_grab) {
                sdl_grab_end();
            }
            if (ev->active.state & SDL_APPACTIVE) {
                if (ev->active.gain) {
                    /* Back to default interval */
                    dcl->gui_timer_interval = 0;
                    dcl->idle = 0;
                } else {
                    /* Sleeping interval */
                    dcl->gui_timer_interval = 500;
                    dcl->idle = 1;
                }
            }
            break;
        default:
            break;
        }
    }
}

static void sdl_fill(DisplayState *ds, int x, int y, int w, int h, uint32_t c)
{
    SDL_Rect dst = { x, y, w, h };
    SDL_FillRect(real_screen, &dst, c);
}

static void sdl_mouse_warp(int x, int y, int on)
{
    if (on) {
        if (!guest_cursor)
            sdl_show_cursor();
        if (gui_grab || kbd_mouse_is_absolute() || absolute_enabled) {
            SDL_SetCursor(guest_sprite);
            SDL_WarpMouse(x, y);
        }
    } else if (gui_grab)
        sdl_hide_cursor();
    guest_cursor = on;
    guest_x = x, guest_y = y;
}

static void sdl_mouse_define(int width, int height, int bpp,
                             int hot_x, int hot_y,
                             uint8_t *image, uint8_t *mask)
{
    uint8_t sprite[256], *line;
    int x, y, dst, bypl, src = 0;
    if (guest_sprite)
        SDL_FreeCursor(guest_sprite);

    memset(sprite, 0, 256);
    bypl = ((width * bpp + 31) >> 5) << 2;
    for (y = 0, dst = 0; y < height; y ++, image += bypl) {
        line = image;
        for (x = 0; x < width; x ++, dst ++) {
            switch (bpp) {
            case 24:
                src = *(line ++); src |= *(line ++); src |= *(line ++);
                break;
            case 16:
            case 15:
                src = *(line ++); src |= *(line ++);
                break;
            case 8:
                src = *(line ++);
                break;
            case 4:
                src = 0xf & (line[x >> 1] >> ((x & 1)) << 2);
                break;
            case 2:
                src = 3 & (line[x >> 2] >> ((x & 3)) << 1);
                break;
            case 1:
                src = 1 & (line[x >> 3] >> (x & 7));
                break;
            }
            if (!src)
                sprite[dst >> 3] |= (1 << (~dst & 7)) & mask[dst >> 3];
        }
    }
    guest_sprite = SDL_CreateCursor(sprite, mask, width, height, hot_x, hot_y);

    if (guest_cursor &&
            (gui_grab || kbd_mouse_is_absolute() || absolute_enabled))
        SDL_SetCursor(guest_sprite);
}

static void sdl_cleanup(void)
{
    if (guest_sprite)
        SDL_FreeCursor(guest_sprite);
    SDL_Quit();
}

void sdl_display_init(DisplayState *ds, int full_screen, int no_frame)
{
    int flags;
    uint8_t data = 0;

#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(keyboard_layout);
        if (!kbd_layout)
            exit(1);
    }

    if (no_frame)
        gui_noframe = 1;

    flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL - exiting\n");
        exit(1);
    }

    dcl = qemu_mallocz(sizeof(DisplayChangeListener));
    if (!dcl)
        exit(1);
    dcl->dpy_update = sdl_update;
    dcl->dpy_resize = sdl_resize;
    dcl->dpy_refresh = sdl_refresh;
    dcl->dpy_setdata = sdl_setdata;
    dcl->dpy_fill = sdl_fill;
    ds->mouse_set = sdl_mouse_warp;
    ds->cursor_define = sdl_mouse_define;
    register_displaychangelistener(ds, dcl);

    sdl_update_caption();
    SDL_EnableKeyRepeat(250, 50);
    gui_grab = 0;

    sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
    sdl_cursor_normal = SDL_GetCursor();

    atexit(sdl_cleanup);
    if (full_screen) {
        gui_fullscreen = 1;
        gui_fullscreen_initial_grab = 1;
        sdl_grab_start();
    }
}
