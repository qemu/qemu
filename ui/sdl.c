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

/* Avoid compiler warning because macro is redefined in SDL_syswm.h. */
#undef WIN32_LEAN_AND_MEAN

#include "qemu/osdep.h"
#include <SDL.h>
#include <SDL_syswm.h>

#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/cutils.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#ifndef WIN32
#include "x_keymap.h"
#endif
#include "sdl_zoom.h"

static DisplayChangeListener *dcl;
static DisplaySurface *surface;
static DisplayOptions *opts;
static SDL_Surface *real_screen;
static SDL_Surface *guest_screen = NULL;
static int gui_grab; /* if true, all keyboard/mouse events are grabbed */
static int last_vm_running;
static bool gui_saved_scaling;
static int gui_saved_width;
static int gui_saved_height;
static int gui_saved_grab;
static int gui_fullscreen;
static int gui_key_modifier_pressed;
static int gui_keysym;
static int gui_grab_code = KMOD_LALT | KMOD_LCTRL;
static uint8_t modifiers_state[256];
static SDL_Cursor *sdl_cursor_normal;
static SDL_Cursor *sdl_cursor_hidden;
static int absolute_enabled = 0;
static int guest_cursor = 0;
static int guest_x, guest_y;
static SDL_Cursor *guest_sprite = NULL;
static SDL_PixelFormat host_format;
static int scaling_active = 0;
static Notifier mouse_mode_notifier;
static int idle_counter;
static const guint16 *keycode_map;
static size_t keycode_maplen;

#define SDL_REFRESH_INTERVAL_BUSY 10
#define SDL_MAX_IDLE_COUNT (2 * GUI_REFRESH_INTERVAL_DEFAULT \
                            / SDL_REFRESH_INTERVAL_BUSY + 1)

#if 0
#define DEBUG_SDL
#endif

static void sdl_update(DisplayChangeListener *dcl,
                       int x, int y, int w, int h)
{
    SDL_Rect rec;
    rec.x = x;
    rec.y = y;
    rec.w = w;
    rec.h = h;

#ifdef DEBUG_SDL
    printf("SDL: Updating x=%d y=%d w=%d h=%d (scaling: %d)\n",
           x, y, w, h, scaling_active);
#endif

    if (guest_screen) {
        if (!scaling_active) {
            SDL_BlitSurface(guest_screen, &rec, real_screen, &rec);
        } else {
            if (sdl_zoom_blit(guest_screen, real_screen, SMOOTHING_ON, &rec) < 0) {
                fprintf(stderr, "Zoom blit failed\n");
                exit(1);
            }
        }
    } 
    SDL_UpdateRect(real_screen, rec.x, rec.y, rec.w, rec.h);
}

static void do_sdl_resize(int width, int height, int bpp)
{
    int flags;
    SDL_Surface *tmp_screen;

#ifdef DEBUG_SDL
    printf("SDL: Resizing to %dx%d bpp %d\n", width, height, bpp);
#endif

    flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
    if (gui_fullscreen) {
        flags |= SDL_FULLSCREEN;
    } else {
        flags |= SDL_RESIZABLE;
    }
    if (no_frame) {
        flags |= SDL_NOFRAME;
    }

    tmp_screen = SDL_SetVideoMode(width, height, bpp, flags);
    if (!real_screen) {
        if (!tmp_screen) {
            fprintf(stderr, "Could not open SDL display (%dx%dx%d): %s\n",
                    width, height, bpp, SDL_GetError());
            exit(1);
        }
    } else {
        /*
         * Revert to the previous video mode if the change of resizing or
         * resolution failed.
         */
        if (!tmp_screen) {
            fprintf(stderr, "Failed to set SDL display (%dx%dx%d): %s\n",
                    width, height, bpp, SDL_GetError());
            return;
        }
    }

    real_screen = tmp_screen;
}

static void sdl_switch(DisplayChangeListener *dcl,
                       DisplaySurface *new_surface)
{
    PixelFormat pf;

    /* temporary hack: allows to call sdl_switch to handle scaling changes */
    if (new_surface) {
        surface = new_surface;
    }
    pf = qemu_pixelformat_from_pixman(surface->format);

    if (!scaling_active) {
        do_sdl_resize(surface_width(surface), surface_height(surface), 0);
    } else if (real_screen->format->BitsPerPixel !=
               surface_bits_per_pixel(surface)) {
        do_sdl_resize(real_screen->w, real_screen->h,
                      surface_bits_per_pixel(surface));
    }

    if (guest_screen != NULL) {
        SDL_FreeSurface(guest_screen);
    }

#ifdef DEBUG_SDL
    printf("SDL: Creating surface with masks: %08x %08x %08x %08x\n",
           pf.rmask, pf.gmask, pf.bmask, pf.amask);
#endif

    guest_screen = SDL_CreateRGBSurfaceFrom
        (surface_data(surface),
         surface_width(surface), surface_height(surface),
         surface_bits_per_pixel(surface), surface_stride(surface),
         pf.rmask, pf.gmask,
         pf.bmask, pf.amask);
}

static bool sdl_check_format(DisplayChangeListener *dcl,
                             pixman_format_code_t format)
{
    /*
     * We let SDL convert for us a few more formats than,
     * the native ones. Thes are the ones I have tested.
     */
    return (format == PIXMAN_x8r8g8b8 ||
            format == PIXMAN_b8g8r8x8 ||
            format == PIXMAN_x1r5g5b5 ||
            format == PIXMAN_r5g6b5);
}

/* generic keyboard conversion */

#include "sdl_keysym.h"

static kbd_layout_t *kbd_layout = NULL;

static uint8_t sdl_keyevent_to_keycode_generic(const SDL_KeyboardEvent *ev)
{
    bool shift = modifiers_state[0x2a] || modifiers_state[0x36];
    bool altgr = modifiers_state[0xb8];
    bool ctrl  = modifiers_state[0x1d] || modifiers_state[0x9d];
    int keysym;
    /* workaround for X11+SDL bug with AltGR */
    keysym = ev->keysym.sym;
    if (keysym == 0 && ev->keysym.scancode == 113)
        keysym = SDLK_MODE;
    /* For Japanese key '\' and '|' */
    if (keysym == 92 && ev->keysym.scancode == 133) {
        keysym = 0xa5;
    }
    return keysym2scancode(kbd_layout, keysym,
                           shift, altgr, ctrl) & SCANCODE_KEYMASK;
}


static const guint16 *sdl_get_keymap(size_t *maplen)
{
#if defined(WIN32)
    *maplen = qemu_input_map_atset1_to_qcode_len;
    return qemu_input_map_atset1_to_qcode;
#else
#if defined(SDL_VIDEO_DRIVER_X11)
    SDL_SysWMinfo info;

    SDL_VERSION(&info.version);
    if (SDL_GetWMInfo(&info) > 0) {
        return qemu_xkeymap_mapping_table(
            info.info.x11.display, maplen);
    }
#endif
    g_warning("Unsupported SDL video driver / platform.\n"
              "Assuming Linux KBD scancodes, but probably wrong.\n"
              "Please report to qemu-devel@nongnu.org\n"
              "including the following information:\n"
              "\n"
              "  - Operating system\n"
              "  - SDL video driver\n");
    *maplen = qemu_input_map_xorgkbd_to_qcode_len;
    return qemu_input_map_xorgkbd_to_qcode;
#endif
}

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    int qcode;
    if (!keycode_map) {
        return 0;
    }
    if (ev->keysym.scancode > keycode_maplen) {
        return 0;
    }

    qcode = keycode_map[ev->keysym.scancode];

    if (qcode > qemu_input_map_qcode_to_qnum_len) {
        return 0;
    }

    return qemu_input_map_qcode_to_qnum[qcode];
}

static void reset_keys(void)
{
    int i;
    for(i = 0; i < 256; i++) {
        if (modifiers_state[i]) {
            qemu_input_event_send_key_number(dcl->con, i, false);
            modifiers_state[i] = 0;
        }
    }
}

static void sdl_process_key(SDL_KeyboardEvent *ev)
{
    int keycode;

    if (ev->keysym.sym == SDLK_PAUSE) {
        /* specific case */
        qemu_input_event_send_key_qcode(dcl->con, Q_KEY_CODE_PAUSE,
                                        ev->type == SDL_KEYDOWN);
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
#define QEMU_SDL_VERSION ((SDL_MAJOR_VERSION << 8) + SDL_MINOR_VERSION)
#if QEMU_SDL_VERSION < 0x102 || QEMU_SDL_VERSION == 0x102 && SDL_PATCHLEVEL < 14
        /* SDL versions before 1.2.14 don't support key up for caps/num lock. */
    case 0x45: /* num lock */
    case 0x3a: /* caps lock */
        /* SDL does not send the key up event, so we generate it */
        qemu_input_event_send_key_number(dcl->con, keycode, true);
        qemu_input_event_send_key_number(dcl->con, keycode, false);
        return;
#endif
    }

    /* now send the key code */
    qemu_input_event_send_key_number(dcl->con, keycode,
                                     ev->type == SDL_KEYDOWN);
}

static void sdl_update_caption(void)
{
    char win_title[1024];
    char icon_title[1024];
    const char *status = "";

    if (!runstate_is_running())
        status = " [Stopped]";
    else if (gui_grab) {
        if (alt_grab)
            status = " - Press Ctrl-Alt-Shift-G to exit mouse grab";
        else if (ctrl_grab)
            status = " - Press Right-Ctrl-G to exit mouse grab";
        else
            status = " - Press Ctrl-Alt-G to exit mouse grab";
    }

    if (qemu_name) {
        snprintf(win_title, sizeof(win_title), "QEMU (%s)%s", qemu_name, status);
        snprintf(icon_title, sizeof(icon_title), "QEMU (%s)", qemu_name);
    } else {
        snprintf(win_title, sizeof(win_title), "QEMU%s", status);
        snprintf(icon_title, sizeof(icon_title), "QEMU");
    }

    SDL_WM_SetCaption(win_title, icon_title);
}

static void sdl_hide_cursor(void)
{
    if (!cursor_hide)
        return;

    if (qemu_input_is_absolute()) {
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

    if (!qemu_input_is_absolute() || !qemu_console_is_graphic(NULL)) {
        SDL_ShowCursor(1);
        if (guest_cursor &&
                (gui_grab || qemu_input_is_absolute() || absolute_enabled))
            SDL_SetCursor(guest_sprite);
        else
            SDL_SetCursor(sdl_cursor_normal);
    }
}

static void sdl_grab_start(void)
{
    /*
     * If the application is not active, do not try to enter grab state. This
     * prevents 'SDL_WM_GrabInput(SDL_GRAB_ON)' from blocking all the
     * application (SDL bug).
     */
    if (!(SDL_GetAppState() & SDL_APPINPUTFOCUS)) {
        return;
    }
    if (guest_cursor) {
        SDL_SetCursor(guest_sprite);
        if (!qemu_input_is_absolute() && !absolute_enabled) {
            SDL_WarpMouse(guest_x, guest_y);
        }
    } else
        sdl_hide_cursor();
    SDL_WM_GrabInput(SDL_GRAB_ON);
    gui_grab = 1;
    sdl_update_caption();
}

static void sdl_grab_end(void)
{
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    gui_grab = 0;
    sdl_show_cursor();
    sdl_update_caption();
}

static void absolute_mouse_grab(void)
{
    int mouse_x, mouse_y;

    SDL_GetMouseState(&mouse_x, &mouse_y);
    if (mouse_x > 0 && mouse_x < real_screen->w - 1 &&
        mouse_y > 0 && mouse_y < real_screen->h - 1) {
        sdl_grab_start();
    }
}

static void sdl_mouse_mode_change(Notifier *notify, void *data)
{
    if (qemu_input_is_absolute()) {
        if (!absolute_enabled) {
            absolute_enabled = 1;
            if (qemu_console_is_graphic(NULL)) {
                absolute_mouse_grab();
            }
        }
    } else if (absolute_enabled) {
        if (!gui_fullscreen) {
            sdl_grab_end();
        }
        absolute_enabled = 0;
    }
}

static void sdl_send_mouse_event(int dx, int dy, int x, int y, int state)
{
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]       = SDL_BUTTON(SDL_BUTTON_LEFT),
        [INPUT_BUTTON_MIDDLE]     = SDL_BUTTON(SDL_BUTTON_MIDDLE),
        [INPUT_BUTTON_RIGHT]      = SDL_BUTTON(SDL_BUTTON_RIGHT),
        [INPUT_BUTTON_WHEEL_UP]   = SDL_BUTTON(SDL_BUTTON_WHEELUP),
        [INPUT_BUTTON_WHEEL_DOWN] = SDL_BUTTON(SDL_BUTTON_WHEELDOWN),
    };
    static uint32_t prev_state;

    if (prev_state != state) {
        qemu_input_update_buttons(dcl->con, bmap, prev_state, state);
        prev_state = state;
    }

    if (qemu_input_is_absolute()) {
        qemu_input_queue_abs(dcl->con, INPUT_AXIS_X, x,
                             0, real_screen->w);
        qemu_input_queue_abs(dcl->con, INPUT_AXIS_Y, y,
                             0, real_screen->h);
    } else {
        if (guest_cursor) {
            x -= guest_x;
            y -= guest_y;
            guest_x += x;
            guest_y += y;
            dx = x;
            dy = y;
        }
        qemu_input_queue_rel(dcl->con, INPUT_AXIS_X, dx);
        qemu_input_queue_rel(dcl->con, INPUT_AXIS_Y, dy);
    }
    qemu_input_event_sync();
}

static void sdl_scale(int width, int height)
{
    int bpp = real_screen->format->BitsPerPixel;

#ifdef DEBUG_SDL
    printf("SDL: Scaling to %dx%d bpp %d\n", width, height, bpp);
#endif

    if (bpp != 16 && bpp != 32) {
        bpp = 32;
    }
    do_sdl_resize(width, height, bpp);
    scaling_active = 1;
}

static void toggle_full_screen(void)
{
    int width = surface_width(surface);
    int height = surface_height(surface);
    int bpp = surface_bits_per_pixel(surface);

    gui_fullscreen = !gui_fullscreen;
    if (gui_fullscreen) {
        gui_saved_width = real_screen->w;
        gui_saved_height = real_screen->h;
        gui_saved_scaling = scaling_active;

        do_sdl_resize(width, height, bpp);
        scaling_active = 0;

        gui_saved_grab = gui_grab;
        sdl_grab_start();
    } else {
        if (gui_saved_scaling) {
            sdl_scale(gui_saved_width, gui_saved_height);
        } else {
            do_sdl_resize(width, height, 0);
        }
        if (!gui_saved_grab || !qemu_console_is_graphic(NULL)) {
            sdl_grab_end();
        }
    }
    graphic_hw_invalidate(NULL);
    graphic_hw_update(NULL);
}

static void handle_keydown(SDL_Event *ev)
{
    int mod_state;
    int keycode;

    if (alt_grab) {
        mod_state = (SDL_GetModState() & (gui_grab_code | KMOD_LSHIFT)) ==
                    (gui_grab_code | KMOD_LSHIFT);
    } else if (ctrl_grab) {
        mod_state = (SDL_GetModState() & KMOD_RCTRL) == KMOD_RCTRL;
    } else {
        mod_state = (SDL_GetModState() & gui_grab_code) == gui_grab_code;
    }
    gui_key_modifier_pressed = mod_state;

    if (gui_key_modifier_pressed) {
        keycode = sdl_keyevent_to_keycode(&ev->key);
        switch (keycode) {
        case 0x21: /* 'f' key on US keyboard */
            toggle_full_screen();
            gui_keysym = 1;
            break;
        case 0x22: /* 'g' key */
            if (!gui_grab) {
                if (qemu_console_is_graphic(NULL)) {
                    sdl_grab_start();
                }
            } else if (!gui_fullscreen) {
                sdl_grab_end();
            }
            gui_keysym = 1;
            break;
        case 0x16: /* 'u' key on US keyboard */
            if (scaling_active) {
                scaling_active = 0;
                sdl_switch(dcl, NULL);
                graphic_hw_invalidate(NULL);
                graphic_hw_update(NULL);
            }
            gui_keysym = 1;
            break;
        case 0x02 ... 0x0a: /* '1' to '9' keys */
            /* Reset the modifiers sent to the current console */
            reset_keys();
            console_select(keycode - 0x02);
            gui_keysym = 1;
            if (gui_fullscreen) {
                break;
            }
            if (!qemu_console_is_graphic(NULL)) {
                /* release grab if going to a text console */
                if (gui_grab) {
                    sdl_grab_end();
                } else if (absolute_enabled) {
                    sdl_show_cursor();
                }
            } else if (absolute_enabled) {
                sdl_hide_cursor();
                absolute_mouse_grab();
            }
            break;
        case 0x1b: /* '+' */
        case 0x35: /* '-' */
            if (!gui_fullscreen) {
                int width = MAX(real_screen->w + (keycode == 0x1b ? 50 : -50),
                                160);
                int height = (surface_height(surface) * width) /
                    surface_width(surface);

                sdl_scale(width, height);
                graphic_hw_invalidate(NULL);
                graphic_hw_update(NULL);
                gui_keysym = 1;
            }
        default:
            break;
        }
    } else if (!qemu_console_is_graphic(NULL)) {
        int keysym = 0;

        if (ev->key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
            switch (ev->key.keysym.sym) {
            case SDLK_UP:
                keysym = QEMU_KEY_CTRL_UP;
                break;
            case SDLK_DOWN:
                keysym = QEMU_KEY_CTRL_DOWN;
                break;
            case SDLK_LEFT:
                keysym = QEMU_KEY_CTRL_LEFT;
                break;
            case SDLK_RIGHT:
                keysym = QEMU_KEY_CTRL_RIGHT;
                break;
            case SDLK_HOME:
                keysym = QEMU_KEY_CTRL_HOME;
                break;
            case SDLK_END:
                keysym = QEMU_KEY_CTRL_END;
                break;
            case SDLK_PAGEUP:
                keysym = QEMU_KEY_CTRL_PAGEUP;
                break;
            case SDLK_PAGEDOWN:
                keysym = QEMU_KEY_CTRL_PAGEDOWN;
                break;
            default:
                break;
            }
        } else {
            switch (ev->key.keysym.sym) {
            case SDLK_UP:
                keysym = QEMU_KEY_UP;
                break;
            case SDLK_DOWN:
                keysym = QEMU_KEY_DOWN;
                break;
            case SDLK_LEFT:
                keysym = QEMU_KEY_LEFT;
                break;
            case SDLK_RIGHT:
                keysym = QEMU_KEY_RIGHT;
                break;
            case SDLK_HOME:
                keysym = QEMU_KEY_HOME;
                break;
            case SDLK_END:
                keysym = QEMU_KEY_END;
                break;
            case SDLK_PAGEUP:
                keysym = QEMU_KEY_PAGEUP;
                break;
            case SDLK_PAGEDOWN:
                keysym = QEMU_KEY_PAGEDOWN;
                break;
            case SDLK_BACKSPACE:
                keysym = QEMU_KEY_BACKSPACE;
                break;
            case SDLK_DELETE:
                keysym = QEMU_KEY_DELETE;
                break;
            default:
                break;
            }
        }
        if (keysym) {
            kbd_put_keysym(keysym);
        } else if (ev->key.keysym.unicode != 0) {
            kbd_put_keysym(ev->key.keysym.unicode);
        }
    }
    if (qemu_console_is_graphic(NULL) && !gui_keysym) {
        sdl_process_key(&ev->key);
    }
}

static void handle_keyup(SDL_Event *ev)
{
    int mod_state;

    if (!alt_grab) {
        mod_state = (ev->key.keysym.mod & gui_grab_code);
    } else {
        mod_state = (ev->key.keysym.mod & (gui_grab_code | KMOD_LSHIFT));
    }
    if (!mod_state && gui_key_modifier_pressed) {
        gui_key_modifier_pressed = 0;
        gui_keysym = 0;
    }
    if (qemu_console_is_graphic(NULL) && !gui_keysym) {
        sdl_process_key(&ev->key);
    }
}

static void handle_mousemotion(SDL_Event *ev)
{
    int max_x, max_y;

    if (qemu_console_is_graphic(NULL) &&
        (qemu_input_is_absolute() || absolute_enabled)) {
        max_x = real_screen->w - 1;
        max_y = real_screen->h - 1;
        if (gui_grab && (ev->motion.x == 0 || ev->motion.y == 0 ||
            ev->motion.x == max_x || ev->motion.y == max_y)) {
            sdl_grab_end();
        }
        if (!gui_grab &&
            (ev->motion.x > 0 && ev->motion.x < max_x &&
            ev->motion.y > 0 && ev->motion.y < max_y)) {
            sdl_grab_start();
        }
    }
    if (gui_grab || qemu_input_is_absolute() || absolute_enabled) {
        sdl_send_mouse_event(ev->motion.xrel, ev->motion.yrel,
                             ev->motion.x, ev->motion.y, ev->motion.state);
    }
}

static void handle_mousebutton(SDL_Event *ev)
{
    int buttonstate = SDL_GetMouseState(NULL, NULL);
    SDL_MouseButtonEvent *bev;

    if (!qemu_console_is_graphic(NULL)) {
        return;
    }

    bev = &ev->button;
    if (!gui_grab && !qemu_input_is_absolute()) {
        if (ev->type == SDL_MOUSEBUTTONUP && bev->button == SDL_BUTTON_LEFT) {
            /* start grabbing all events */
            sdl_grab_start();
        }
    } else {
        if (ev->type == SDL_MOUSEBUTTONDOWN) {
            buttonstate |= SDL_BUTTON(bev->button);
        } else {
            buttonstate &= ~SDL_BUTTON(bev->button);
        }
        sdl_send_mouse_event(0, 0, bev->x, bev->y, buttonstate);
    }
}

static void handle_activation(SDL_Event *ev)
{
#ifdef _WIN32
    /* Disable grab if the window no longer has the focus
     * (Windows-only workaround) */
    if (gui_grab && ev->active.state == SDL_APPINPUTFOCUS &&
        !ev->active.gain && !gui_fullscreen) {
        sdl_grab_end();
    }
#endif
    if (!gui_grab && ev->active.gain && qemu_console_is_graphic(NULL) &&
        (qemu_input_is_absolute() || absolute_enabled)) {
        absolute_mouse_grab();
    }
    if (ev->active.state & SDL_APPACTIVE) {
        if (ev->active.gain) {
            /* Back to default interval */
            update_displaychangelistener(dcl, GUI_REFRESH_INTERVAL_DEFAULT);
        } else {
            /* Sleeping interval.  Not using the long default here as
             * sdl_refresh does not only update the guest screen, but
             * also checks for gui events. */
            update_displaychangelistener(dcl, 500);
        }
    }
}

static void sdl_refresh(DisplayChangeListener *dcl)
{
    SDL_Event ev1, *ev = &ev1;
    bool allow_close = true;
    int idle = 1;

    if (last_vm_running != runstate_is_running()) {
        last_vm_running = runstate_is_running();
        sdl_update_caption();
    }

    graphic_hw_update(NULL);
    SDL_EnableUNICODE(!qemu_console_is_graphic(NULL));

    while (SDL_PollEvent(ev)) {
        switch (ev->type) {
        case SDL_VIDEOEXPOSE:
            sdl_update(dcl, 0, 0, real_screen->w, real_screen->h);
            break;
        case SDL_KEYDOWN:
            idle = 0;
            handle_keydown(ev);
            break;
        case SDL_KEYUP:
            idle = 0;
            handle_keyup(ev);
            break;
        case SDL_QUIT:
            if (opts->has_window_close && !opts->window_close) {
                allow_close = false;
            }
            if (allow_close) {
                no_shutdown = 0;
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_UI);
            }
            break;
        case SDL_MOUSEMOTION:
            idle = 0;
            handle_mousemotion(ev);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            idle = 0;
            handle_mousebutton(ev);
            break;
        case SDL_ACTIVEEVENT:
            handle_activation(ev);
            break;
        case SDL_VIDEORESIZE:
            sdl_scale(ev->resize.w, ev->resize.h);
            graphic_hw_invalidate(NULL);
            graphic_hw_update(NULL);
            break;
        default:
            break;
        }
    }

    if (idle) {
        if (idle_counter < SDL_MAX_IDLE_COUNT) {
            idle_counter++;
            if (idle_counter >= SDL_MAX_IDLE_COUNT) {
                dcl->update_interval = GUI_REFRESH_INTERVAL_DEFAULT;
            }
        }
    } else {
        idle_counter = 0;
        dcl->update_interval = SDL_REFRESH_INTERVAL_BUSY;
    }
}

static void sdl_mouse_warp(DisplayChangeListener *dcl,
                           int x, int y, int on)
{
    if (on) {
        if (!guest_cursor)
            sdl_show_cursor();
        if (gui_grab || qemu_input_is_absolute() || absolute_enabled) {
            SDL_SetCursor(guest_sprite);
            if (!qemu_input_is_absolute() && !absolute_enabled) {
                SDL_WarpMouse(x, y);
            }
        }
    } else if (gui_grab)
        sdl_hide_cursor();
    guest_cursor = on;
    guest_x = x, guest_y = y;
}

static void sdl_mouse_define(DisplayChangeListener *dcl,
                             QEMUCursor *c)
{
    uint8_t *image, *mask;
    int bpl;

    if (guest_sprite)
        SDL_FreeCursor(guest_sprite);

    bpl = cursor_get_mono_bpl(c);
    image = g_malloc0(bpl * c->height);
    mask  = g_malloc0(bpl * c->height);
    cursor_get_mono_image(c, 0x000000, image);
    cursor_get_mono_mask(c, 0, mask);
    guest_sprite = SDL_CreateCursor(image, mask, c->width, c->height,
                                    c->hot_x, c->hot_y);
    g_free(image);
    g_free(mask);

    if (guest_cursor &&
            (gui_grab || qemu_input_is_absolute() || absolute_enabled))
        SDL_SetCursor(guest_sprite);
}

static void sdl_cleanup(void)
{
    if (guest_sprite)
        SDL_FreeCursor(guest_sprite);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name             = "sdl",
    .dpy_gfx_update       = sdl_update,
    .dpy_gfx_switch       = sdl_switch,
    .dpy_gfx_check_format = sdl_check_format,
    .dpy_refresh          = sdl_refresh,
    .dpy_mouse_set        = sdl_mouse_warp,
    .dpy_cursor_define    = sdl_mouse_define,
};

static void sdl1_display_init(DisplayState *ds, DisplayOptions *o)
{
    int flags;
    uint8_t data = 0;
    const SDL_VideoInfo *vi;
    SDL_SysWMinfo info;
    char *filename;

    assert(o->type == DISPLAY_TYPE_SDL);
    opts = o;
#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout,
                                          &error_fatal);
    }

    g_printerr("Running QEMU with SDL 1.2 is deprecated, and will be removed\n"
               "in a future release. Please switch to SDL 2.0 instead\n");

    if (opts->has_full_screen && opts->full_screen) {
        setenv("SDL_VIDEO_ALLOW_SCREENSAVER", "1", 0);
    }
#ifdef __linux__
    /* on Linux, SDL may use fbcon|directfb|svgalib when run without
     * accessible $DISPLAY to open X11 window.  This is often the case
     * when qemu is run using sudo.  But in this case, and when actually
     * run in X11 environment, SDL fights with X11 for the video card,
     * making current display unavailable, often until reboot.
     * So make x11 the default SDL video driver if this variable is unset.
     * This is a bit hackish but saves us from bigger problem.
     * Maybe it's a good idea to fix this in SDL instead.
     */
    setenv("SDL_VIDEODRIVER", "x11", 0);
#endif

    /* Enable normal up/down events for Caps-Lock and Num-Lock keys.
     * This requires SDL >= 1.2.14. */
    setenv("SDL_DISABLE_LOCK_KEYS", "1", 1);

    flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
    if (SDL_Init (flags)) {
        fprintf(stderr, "Could not initialize SDL(%s) - exiting\n",
                SDL_GetError());
        exit(1);
    }
    vi = SDL_GetVideoInfo();
    host_format = *(vi->vfmt);

    keycode_map = sdl_get_keymap(&keycode_maplen);

    /* Load a 32x32x4 image. White pixels are transparent. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "qemu-icon.bmp");
    if (filename) {
        SDL_Surface *image = SDL_LoadBMP(filename);
        if (image) {
            uint32_t colorkey = SDL_MapRGB(image->format, 255, 255, 255);
            SDL_SetColorKey(image, SDL_SRCCOLORKEY, colorkey);
            SDL_WM_SetIcon(image, NULL);
        }
        g_free(filename);
    }

    if (opts->has_full_screen && opts->full_screen) {
        gui_fullscreen = 1;
        sdl_grab_start();
    }

    dcl = g_new0(DisplayChangeListener, 1);
    dcl->ops = &dcl_ops;
    register_displaychangelistener(dcl);

    mouse_mode_notifier.notify = sdl_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&mouse_mode_notifier);

    sdl_update_caption();
    SDL_EnableKeyRepeat(250, 50);
    gui_grab = 0;

    sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
    sdl_cursor_normal = SDL_GetCursor();

    memset(&info, 0, sizeof(info));
    SDL_VERSION(&info.version);
    if (SDL_GetWMInfo(&info)) {
        int i;
        for (i = 0; ; i++) {
            /* All consoles share the same window */
            QemuConsole *con = qemu_console_lookup_by_index(i);
            if (con) {
#if defined(SDL_VIDEO_DRIVER_X11)
                qemu_console_set_window_id(con, info.info.x11.wmwindow);
#elif defined(SDL_VIDEO_DRIVER_NANOX) || \
      defined(SDL_VIDEO_DRIVER_WINDIB) || defined(SDL_VIDEO_DRIVER_DDRAW) || \
      defined(SDL_VIDEO_DRIVER_GAPI) || \
      defined(SDL_VIDEO_DRIVER_RISCOS)
                qemu_console_set_window_id(con, (int) (uintptr_t) info.window);
#else
                qemu_console_set_window_id(con, info.data);
#endif
            } else {
                break;
            }
        }
    }

    atexit(sdl_cleanup);
}

static QemuDisplay qemu_display_sdl1 = {
    .type       = DISPLAY_TYPE_SDL,
    .init       = sdl1_display_init,
};

static void register_sdl1(void)
{
    qemu_display_register(&qemu_display_sdl1);
}

type_init(register_sdl1);
