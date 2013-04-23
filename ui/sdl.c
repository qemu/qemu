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

#include <SDL.h>
#include <SDL_syswm.h>

#include "qemu-common.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"
#include "x_keymap.h"
#include "sdl_zoom.h"

static DisplayChangeListener *dcl;
static DisplaySurface *surface;
static SDL_Surface *real_screen;
static SDL_Surface *guest_screen = NULL;
static int gui_grab; /* if true, all keyboard/mouse events are grabbed */
static int last_vm_running;
static bool gui_saved_scaling;
static int gui_saved_width;
static int gui_saved_height;
static int gui_saved_grab;
static int gui_fullscreen;
static int gui_noframe;
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

static void sdl_update(DisplayChangeListener *dcl,
                       int x, int y, int w, int h)
{
    //    printf("updating x=%d y=%d w=%d h=%d\n", x, y, w, h);
    SDL_Rect rec;
    rec.x = x;
    rec.y = y;
    rec.w = w;
    rec.h = h;

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

    //    printf("resizing to %d %d\n", w, h);

    flags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
    if (gui_fullscreen) {
        flags |= SDL_FULLSCREEN;
    } else {
        flags |= SDL_RESIZABLE;
    }
    if (gui_noframe)
        flags |= SDL_NOFRAME;

    real_screen = SDL_SetVideoMode(width, height, bpp, flags);
    if (!real_screen) {
	fprintf(stderr, "Could not open SDL display (%dx%dx%d): %s\n", width, 
		height, bpp, SDL_GetError());
        exit(1);
    }
}

static void sdl_switch(DisplayChangeListener *dcl,
                       DisplaySurface *new_surface)
{

    /* temporary hack: allows to call sdl_switch to handle scaling changes */
    if (new_surface) {
        surface = new_surface;
    }

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
    guest_screen = SDL_CreateRGBSurfaceFrom
        (surface_data(surface),
         surface_width(surface), surface_height(surface),
         surface_bits_per_pixel(surface), surface_stride(surface),
         surface->pf.rmask, surface->pf.gmask,
         surface->pf.bmask, surface->pf.amask);
}

/* generic keyboard conversion */

#include "sdl_keysym.h"

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
    return keysym2scancode(kbd_layout, keysym) & SCANCODE_KEYMASK;
}

/* specific keyboard conversions from scan codes */

#if defined(_WIN32)

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    return ev->keysym.scancode;
}

#else

#if defined(SDL_VIDEO_DRIVER_X11)
#include <X11/XKBlib.h>

static int check_for_evdev(void)
{
    SDL_SysWMinfo info;
    XkbDescPtr desc = NULL;
    int has_evdev = 0;
    char *keycodes = NULL;

    SDL_VERSION(&info.version);
    if (!SDL_GetWMInfo(&info)) {
        return 0;
    }
    desc = XkbGetKeyboard(info.info.x11.display,
                          XkbGBN_AllComponentsMask,
                          XkbUseCoreKbd);
    if (desc && desc->names) {
        keycodes = XGetAtomName(info.info.x11.display, desc->names->keycodes);
        if (keycodes == NULL) {
            fprintf(stderr, "could not lookup keycode name\n");
        } else if (strstart(keycodes, "evdev", NULL)) {
            has_evdev = 1;
        } else if (!strstart(keycodes, "xfree86", NULL)) {
            fprintf(stderr, "unknown keycodes `%s', please report to "
                    "qemu-devel@nongnu.org\n", keycodes);
        }
    }

    if (desc) {
        XkbFreeKeyboard(desc, XkbGBN_AllComponentsMask, True);
    }
    if (keycodes) {
        XFree(keycodes);
    }
    return has_evdev;
}
#else
static int check_for_evdev(void)
{
	return 0;
}
#endif

static uint8_t sdl_keyevent_to_keycode(const SDL_KeyboardEvent *ev)
{
    int keycode;
    static int has_evdev = -1;

    if (has_evdev == -1)
        has_evdev = check_for_evdev();

    keycode = ev->keysym.scancode;

    if (keycode < 9) {
        keycode = 0;
    } else if (keycode < 97) {
        keycode -= 8; /* just an offset */
    } else if (keycode < 158) {
        /* use conversion table */
        if (has_evdev)
            keycode = translate_evdev_keycode(keycode - 97);
        else
            keycode = translate_xfree86_keycode(keycode - 97);
    } else if (keycode == 208) { /* Hiragana_Katakana */
        keycode = 0x70;
    } else if (keycode == 211) { /* backslash */
        keycode = 0x73;
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
            if (i & SCANCODE_GREY)
                kbd_put_keycode(SCANCODE_EMUL0);
            kbd_put_keycode(i | SCANCODE_UP);
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
            v |= SCANCODE_UP;
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
#define QEMU_SDL_VERSION ((SDL_MAJOR_VERSION << 8) + SDL_MINOR_VERSION)
#if QEMU_SDL_VERSION < 0x102 || QEMU_SDL_VERSION == 0x102 && SDL_PATCHLEVEL < 14
        /* SDL versions before 1.2.14 don't support key up for caps/num lock. */
    case 0x45: /* num lock */
    case 0x3a: /* caps lock */
        /* SDL does not send the key up event, so we generate it */
        kbd_put_keycode(keycode);
        kbd_put_keycode(keycode | SCANCODE_UP);
        return;
#endif
    }

    /* now send the key code */
    if (keycode & SCANCODE_GREY)
        kbd_put_keycode(SCANCODE_EMUL0);
    if (ev->type == SDL_KEYUP)
        kbd_put_keycode(keycode | SCANCODE_UP);
    else
        kbd_put_keycode(keycode & SCANCODE_KEYCODEMASK);
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
            status = " - Press Ctrl-Alt-Shift to exit mouse grab";
        else if (ctrl_grab)
            status = " - Press Right-Ctrl to exit mouse grab";
        else
            status = " - Press Ctrl-Alt to exit mouse grab";
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

    if (!kbd_mouse_is_absolute() || !qemu_console_is_graphic(NULL)) {
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
        if (!kbd_mouse_is_absolute() && !absolute_enabled)
            SDL_WarpMouse(guest_x, guest_y);
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
    if (kbd_mouse_is_absolute()) {
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

static void sdl_send_mouse_event(int dx, int dy, int dz, int x, int y, int state)
{
    int buttons = 0;

    if (state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        buttons |= MOUSE_EVENT_LBUTTON;
    }
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
        buttons |= MOUSE_EVENT_RBUTTON;
    }
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) {
        buttons |= MOUSE_EVENT_MBUTTON;
    }

    if (kbd_mouse_is_absolute()) {
        dx = x * 0x7FFF / (real_screen->w - 1);
        dy = y * 0x7FFF / (real_screen->h - 1);
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

static void sdl_scale(int width, int height)
{
    int bpp = real_screen->format->BitsPerPixel;

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
        if (gui_keysym == 0) {
            /* exit/enter grab if pressing Ctrl-Alt */
            if (!gui_grab) {
                if (qemu_console_is_graphic(NULL)) {
                    sdl_grab_start();
                }
            } else if (!gui_fullscreen) {
                sdl_grab_end();
            }
            /* SDL does not send back all the modifiers key, so we must
             * correct it. */
            reset_keys();
            return;
        }
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
        (kbd_mouse_is_absolute() || absolute_enabled)) {
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
    if (gui_grab || kbd_mouse_is_absolute() || absolute_enabled) {
        sdl_send_mouse_event(ev->motion.xrel, ev->motion.yrel, 0,
                             ev->motion.x, ev->motion.y, ev->motion.state);
    }
}

static void handle_mousebutton(SDL_Event *ev)
{
    int buttonstate = SDL_GetMouseState(NULL, NULL);
    SDL_MouseButtonEvent *bev;
    int dz;

    if (!qemu_console_is_graphic(NULL)) {
        return;
    }

    bev = &ev->button;
    if (!gui_grab && !kbd_mouse_is_absolute()) {
        if (ev->type == SDL_MOUSEBUTTONUP && bev->button == SDL_BUTTON_LEFT) {
            /* start grabbing all events */
            sdl_grab_start();
        }
    } else {
        dz = 0;
        if (ev->type == SDL_MOUSEBUTTONDOWN) {
            buttonstate |= SDL_BUTTON(bev->button);
        } else {
            buttonstate &= ~SDL_BUTTON(bev->button);
        }
#ifdef SDL_BUTTON_WHEELUP
        if (bev->button == SDL_BUTTON_WHEELUP &&
            ev->type == SDL_MOUSEBUTTONDOWN) {
            dz = -1;
        } else if (bev->button == SDL_BUTTON_WHEELDOWN &&
                   ev->type == SDL_MOUSEBUTTONDOWN) {
            dz = 1;
        }
#endif
        sdl_send_mouse_event(0, 0, dz, bev->x, bev->y, buttonstate);
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
        (kbd_mouse_is_absolute() || absolute_enabled)) {
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
            handle_keydown(ev);
            break;
        case SDL_KEYUP:
            handle_keyup(ev);
            break;
        case SDL_QUIT:
            if (!no_quit) {
                no_shutdown = 0;
                qemu_system_shutdown_request();
            }
            break;
        case SDL_MOUSEMOTION:
            handle_mousemotion(ev);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
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
}

static void sdl_mouse_warp(DisplayChangeListener *dcl,
                           int x, int y, int on)
{
    if (on) {
        if (!guest_cursor)
            sdl_show_cursor();
        if (gui_grab || kbd_mouse_is_absolute() || absolute_enabled) {
            SDL_SetCursor(guest_sprite);
            if (!kbd_mouse_is_absolute() && !absolute_enabled)
                SDL_WarpMouse(x, y);
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
            (gui_grab || kbd_mouse_is_absolute() || absolute_enabled))
        SDL_SetCursor(guest_sprite);
}

static void sdl_cleanup(void)
{
    if (guest_sprite)
        SDL_FreeCursor(guest_sprite);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static const DisplayChangeListenerOps dcl_ops = {
    .dpy_name          = "sdl",
    .dpy_gfx_update    = sdl_update,
    .dpy_gfx_switch    = sdl_switch,
    .dpy_refresh       = sdl_refresh,
    .dpy_mouse_set     = sdl_mouse_warp,
    .dpy_cursor_define = sdl_mouse_define,
};

void sdl_display_init(DisplayState *ds, int full_screen, int no_frame)
{
    int flags;
    uint8_t data = 0;
    const SDL_VideoInfo *vi;
    char *filename;

#if defined(__APPLE__)
    /* always use generic keymaps */
    if (!keyboard_layout)
        keyboard_layout = "en-us";
#endif
    if(keyboard_layout) {
        kbd_layout = init_keyboard_layout(name2keysym, keyboard_layout);
        if (!kbd_layout)
            exit(1);
    }

    if (no_frame)
        gui_noframe = 1;

    if (!full_screen) {
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

    if (full_screen) {
        gui_fullscreen = 1;
        sdl_grab_start();
    }

    dcl = g_malloc0(sizeof(DisplayChangeListener));
    dcl->ops = &dcl_ops;
    register_displaychangelistener(dcl);

    mouse_mode_notifier.notify = sdl_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&mouse_mode_notifier);

    sdl_update_caption();
    SDL_EnableKeyRepeat(250, 50);
    gui_grab = 0;

    sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
    sdl_cursor_normal = SDL_GetCursor();

    atexit(sdl_cleanup);
}
