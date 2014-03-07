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
/* Ported SDL 1.2 code to 2.0 by Dave Airlie. */

/* Avoid compiler warning because macro is redefined in SDL_syswm.h. */
#undef WIN32_LEAN_AND_MEAN

#include <SDL.h>

#if SDL_MAJOR_VERSION == 2
#include <SDL_syswm.h>

#include "qemu-common.h"
#include "ui/console.h"
#include "ui/input.h"
#include "sysemu/sysemu.h"
#include "sdl_zoom.h"

#include "sdl2-keymap.h"

static int sdl2_num_outputs;
static struct sdl2_state {
    DisplayChangeListener dcl;
    DisplaySurface *surface;
    SDL_Texture *texture;
    SDL_Window *real_window;
    SDL_Renderer *real_renderer;
    int idx;
    int last_vm_running; /* per console for caption reasons */
    int x, y;
} *sdl2_console;

static SDL_Surface *guest_sprite_surface;
static int gui_grab; /* if true, all keyboard/mouse events are grabbed */

static bool gui_saved_scaling;
static int gui_saved_width;
static int gui_saved_height;
static int gui_saved_grab;
static int gui_fullscreen;
static int gui_noframe;
static int gui_key_modifier_pressed;
static int gui_keysym;
static int gui_grab_code = KMOD_LALT | KMOD_LCTRL;
static uint8_t modifiers_state[SDL_NUM_SCANCODES];
static SDL_Cursor *sdl_cursor_normal;
static SDL_Cursor *sdl_cursor_hidden;
static int absolute_enabled;
static int guest_cursor;
static int guest_x, guest_y;
static SDL_Cursor *guest_sprite;
static int scaling_active;
static Notifier mouse_mode_notifier;

static void sdl_update_caption(struct sdl2_state *scon);

static struct sdl2_state *get_scon_from_window(uint32_t window_id)
{
    int i;
    for (i = 0; i < sdl2_num_outputs; i++) {
        if (sdl2_console[i].real_window == SDL_GetWindowFromID(window_id)) {
            return &sdl2_console[i];
        }
    }
    return NULL;
}

static void sdl_update(DisplayChangeListener *dcl,
                       int x, int y, int w, int h)
{
    struct sdl2_state *scon = container_of(dcl, struct sdl2_state, dcl);
    SDL_Rect rect;
    DisplaySurface *surf = qemu_console_surface(dcl->con);

    if (!surf) {
        return;
    }
    if (!scon->texture) {
        return;
    }

    rect.x = x;
    rect.y = y;
    rect.w = w;
    rect.h = h;

    SDL_UpdateTexture(scon->texture, NULL, surface_data(surf),
                      surface_stride(surf));
    SDL_RenderCopy(scon->real_renderer, scon->texture, &rect, &rect);
    SDL_RenderPresent(scon->real_renderer);
}

static void do_sdl_resize(struct sdl2_state *scon, int width, int height,
                          int bpp)
{
    int flags;

    if (scon->real_window && scon->real_renderer) {
        if (width && height) {
            SDL_RenderSetLogicalSize(scon->real_renderer, width, height);
            SDL_SetWindowSize(scon->real_window, width, height);
        } else {
            SDL_DestroyRenderer(scon->real_renderer);
            SDL_DestroyWindow(scon->real_window);
            scon->real_renderer = NULL;
            scon->real_window = NULL;
        }
    } else {
        if (!width || !height) {
            return;
        }
        flags = 0;
        if (gui_fullscreen) {
            flags |= SDL_WINDOW_FULLSCREEN;
        } else {
            flags |= SDL_WINDOW_RESIZABLE;
        }

        scon->real_window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED,
                                             SDL_WINDOWPOS_UNDEFINED,
                                             width, height, flags);
        scon->real_renderer = SDL_CreateRenderer(scon->real_window, -1, 0);
        sdl_update_caption(scon);
    }
}

static void sdl_switch(DisplayChangeListener *dcl,
                       DisplaySurface *new_surface)
{
    struct sdl2_state *scon = container_of(dcl, struct sdl2_state, dcl);
    int format = 0;
    int idx = scon->idx;
    DisplaySurface *old_surface = scon->surface;

    /* temporary hack: allows to call sdl_switch to handle scaling changes */
    if (new_surface) {
        scon->surface = new_surface;
    }

    if (!new_surface && idx > 0) {
        scon->surface = NULL;
    }

    if (new_surface == NULL) {
        do_sdl_resize(scon, 0, 0, 0);
    } else {
        do_sdl_resize(scon, surface_width(scon->surface),
                      surface_height(scon->surface), 0);
    }

    if (old_surface && scon->texture) {
        SDL_DestroyTexture(scon->texture);
        scon->texture = NULL;
    }

    if (new_surface) {
        if (!scon->texture) {
            if (surface_bits_per_pixel(scon->surface) == 16) {
                format = SDL_PIXELFORMAT_RGB565;
            } else if (surface_bits_per_pixel(scon->surface) == 32) {
                format = SDL_PIXELFORMAT_ARGB8888;
            }

            scon->texture = SDL_CreateTexture(scon->real_renderer, format,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              surface_width(new_surface),
                                              surface_height(new_surface));
        }
    }
}

static void reset_keys(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        if (modifiers_state[i]) {
            int qcode = sdl2_scancode_to_qcode[i];
            qemu_input_event_send_key_qcode(NULL, qcode, false);
            modifiers_state[i] = 0;
        }
    }
}

static void sdl_process_key(SDL_KeyboardEvent *ev)
{
    int qcode = sdl2_scancode_to_qcode[ev->keysym.scancode];

    switch (ev->keysym.scancode) {
#if 0
    case SDL_SCANCODE_NUMLOCKCLEAR:
    case SDL_SCANCODE_CAPSLOCK:
        /* SDL does not send the key up event, so we generate it */
        qemu_input_event_send_key_qcode(NULL, qcode, true);
        qemu_input_event_send_key_qcode(NULL, qcode, false);
        return;
#endif
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_RSHIFT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_RGUI:
        if (ev->type == SDL_KEYUP) {
            modifiers_state[ev->keysym.scancode] = 0;
        } else {
            modifiers_state[ev->keysym.scancode] = 1;
        }
        /* fall though */
    default:
        qemu_input_event_send_key_qcode(NULL, qcode,
                                        ev->type == SDL_KEYDOWN);
    }
}

static void sdl_update_caption(struct sdl2_state *scon)
{
    char win_title[1024];
    char icon_title[1024];
    const char *status = "";

    if (!runstate_is_running()) {
        status = " [Stopped]";
    } else if (gui_grab) {
        if (alt_grab) {
            status = " - Press Ctrl-Alt-Shift to exit mouse grab";
        } else if (ctrl_grab) {
            status = " - Press Right-Ctrl to exit mouse grab";
        } else {
            status = " - Press Ctrl-Alt to exit mouse grab";
        }
    }

    if (qemu_name) {
        snprintf(win_title, sizeof(win_title), "QEMU (%s-%d)%s", qemu_name,
                 scon->idx, status);
        snprintf(icon_title, sizeof(icon_title), "QEMU (%s)", qemu_name);
    } else {
        snprintf(win_title, sizeof(win_title), "QEMU%s", status);
        snprintf(icon_title, sizeof(icon_title), "QEMU");
    }

    if (scon->real_window) {
        SDL_SetWindowTitle(scon->real_window, win_title);
    }
}

static void sdl_hide_cursor(void)
{
    if (!cursor_hide) {
        return;
    }

    if (qemu_input_is_absolute()) {
        SDL_ShowCursor(1);
        SDL_SetCursor(sdl_cursor_hidden);
    } else {
        SDL_ShowCursor(0);
    }
}

static void sdl_show_cursor(void)
{
    if (!cursor_hide) {
        return;
    }

    if (!qemu_input_is_absolute()) {
        SDL_ShowCursor(1);
        if (guest_cursor &&
            (gui_grab || qemu_input_is_absolute() || absolute_enabled)) {
            SDL_SetCursor(guest_sprite);
        } else {
            SDL_SetCursor(sdl_cursor_normal);
        }
    }
}

static void sdl_grab_start(struct sdl2_state *scon)
{
    /*
     * If the application is not active, do not try to enter grab state. This
     * prevents 'SDL_WM_GrabInput(SDL_GRAB_ON)' from blocking all the
     * application (SDL bug).
     */
    if (!(SDL_GetWindowFlags(scon->real_window) & SDL_WINDOW_INPUT_FOCUS)) {
        return;
    }
    if (guest_cursor) {
        SDL_SetCursor(guest_sprite);
        if (!qemu_input_is_absolute() && !absolute_enabled) {
            SDL_WarpMouseInWindow(scon->real_window, guest_x, guest_y);
        }
    } else {
        sdl_hide_cursor();
    }
    SDL_SetWindowGrab(scon->real_window, SDL_TRUE);
    gui_grab = 1;
    sdl_update_caption(scon);
}

static void sdl_grab_end(struct sdl2_state *scon)
{
    SDL_SetWindowGrab(scon->real_window, SDL_FALSE);
    gui_grab = 0;
    sdl_show_cursor();
    sdl_update_caption(scon);
}

static void absolute_mouse_grab(struct sdl2_state *scon)
{
    int mouse_x, mouse_y;
    int scr_w, scr_h;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    SDL_GetWindowSize(scon->real_window, &scr_w, &scr_h);
    if (mouse_x > 0 && mouse_x < scr_w - 1 &&
        mouse_y > 0 && mouse_y < scr_h - 1) {
        sdl_grab_start(scon);
    }
}

static void sdl_mouse_mode_change(Notifier *notify, void *data)
{
    if (qemu_input_is_absolute()) {
        if (!absolute_enabled) {
            absolute_enabled = 1;
            absolute_mouse_grab(&sdl2_console[0]);
        }
    } else if (absolute_enabled) {
        if (!gui_fullscreen) {
            sdl_grab_end(&sdl2_console[0]);
        }
        absolute_enabled = 0;
    }
}

static void sdl_send_mouse_event(struct sdl2_state *scon, int dx, int dy,
                                 int dz, int x, int y, int state)
{
    static uint32_t bmap[INPUT_BUTTON_MAX] = {
        [INPUT_BUTTON_LEFT]       = SDL_BUTTON(SDL_BUTTON_LEFT),
        [INPUT_BUTTON_MIDDLE]     = SDL_BUTTON(SDL_BUTTON_MIDDLE),
        [INPUT_BUTTON_RIGHT]      = SDL_BUTTON(SDL_BUTTON_RIGHT),
#if 0
        [INPUT_BUTTON_WHEEL_UP]   = SDL_BUTTON(SDL_BUTTON_WHEELUP),
        [INPUT_BUTTON_WHEEL_DOWN] = SDL_BUTTON(SDL_BUTTON_WHEELDOWN),
#endif
    };
    static uint32_t prev_state;

    if (prev_state != state) {
        qemu_input_update_buttons(scon->dcl.con, bmap, prev_state, state);
        prev_state = state;
    }

    if (qemu_input_is_absolute()) {
        int scr_w, scr_h;
        int max_w = 0, max_h = 0;
        int off_x = 0, off_y = 0;
        int cur_off_x = 0, cur_off_y = 0;
        int i;

        for (i = 0; i < sdl2_num_outputs; i++) {
            struct sdl2_state *thiscon = &sdl2_console[i];
            if (thiscon->real_window && thiscon->surface) {
                SDL_GetWindowSize(thiscon->real_window, &scr_w, &scr_h);
                cur_off_x = thiscon->x;
                cur_off_y = thiscon->y;
                if (scr_w + cur_off_x > max_w) {
                    max_w = scr_w + cur_off_x;
                }
                if (scr_h + cur_off_y > max_h) {
                    max_h = scr_h + cur_off_y;
                }
                if (i == scon->idx) {
                    off_x = cur_off_x;
                    off_y = cur_off_y;
                }
            }
        }
        qemu_input_queue_abs(scon->dcl.con, INPUT_AXIS_X, off_x + x, max_w);
        qemu_input_queue_abs(scon->dcl.con, INPUT_AXIS_Y, off_y + y, max_h);
    } else if (guest_cursor) {
        x -= guest_x;
        y -= guest_y;
        guest_x += x;
        guest_y += y;
        qemu_input_queue_rel(scon->dcl.con, INPUT_AXIS_X, x);
        qemu_input_queue_rel(scon->dcl.con, INPUT_AXIS_Y, y);
    }
    qemu_input_event_sync();
}

static void sdl_scale(struct sdl2_state *scon, int width, int height)
{
    int bpp = 0;
    do_sdl_resize(scon, width, height, bpp);
    scaling_active = 1;
}

static void toggle_full_screen(struct sdl2_state *scon)
{
    int width = surface_width(scon->surface);
    int height = surface_height(scon->surface);
    int bpp = surface_bits_per_pixel(scon->surface);

    gui_fullscreen = !gui_fullscreen;
    if (gui_fullscreen) {
        SDL_GetWindowSize(scon->real_window,
                          &gui_saved_width, &gui_saved_height);
        gui_saved_scaling = scaling_active;

        do_sdl_resize(scon, width, height, bpp);
        scaling_active = 0;

        gui_saved_grab = gui_grab;
        sdl_grab_start(scon);
    } else {
        if (gui_saved_scaling) {
            sdl_scale(scon, gui_saved_width, gui_saved_height);
        } else {
            do_sdl_resize(scon, width, height, 0);
        }
        if (!gui_saved_grab) {
            sdl_grab_end(scon);
        }
    }
    graphic_hw_invalidate(scon->dcl.con);
    graphic_hw_update(scon->dcl.con);
}

static void handle_keydown(SDL_Event *ev)
{
    int mod_state;
    struct sdl2_state *scon = get_scon_from_window(ev->key.windowID);

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
        switch (ev->key.keysym.scancode) {
        case SDL_SCANCODE_F:
            toggle_full_screen(scon);
            gui_keysym = 1;
            break;
        case SDL_SCANCODE_U:
            if (scaling_active) {
                scaling_active = 0;
                sdl_switch(&scon->dcl, NULL);
                graphic_hw_invalidate(scon->dcl.con);
                graphic_hw_update(scon->dcl.con);
            }
            gui_keysym = 1;
            break;
        case SDL_SCANCODE_KP_PLUS:
        case SDL_SCANCODE_KP_MINUS:
            if (!gui_fullscreen) {
                int scr_w, scr_h;
                int width, height;
                SDL_GetWindowSize(scon->real_window, &scr_w, &scr_h);

                width = MAX(scr_w + (ev->key.keysym.scancode ==
                                     SDL_SCANCODE_KP_PLUS ? 50 : -50),
                            160);
                height = (surface_height(scon->surface) * width) /
                    surface_width(scon->surface);

                sdl_scale(scon, width, height);
                graphic_hw_invalidate(NULL);
                graphic_hw_update(NULL);
                gui_keysym = 1;
            }
        default:
            break;
        }
    }
    if (!gui_keysym) {
        sdl_process_key(&ev->key);
    }
}

static void handle_keyup(SDL_Event *ev)
{
    int mod_state;
    struct sdl2_state *scon = get_scon_from_window(ev->key.windowID);

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
                sdl_grab_start(scon);
            } else if (!gui_fullscreen) {
                sdl_grab_end(scon);
            }
            /* SDL does not send back all the modifiers key, so we must
             * correct it. */
            reset_keys();
            return;
        }
        gui_keysym = 0;
    }
    if (!gui_keysym) {
        sdl_process_key(&ev->key);
    }
}

static void handle_mousemotion(SDL_Event *ev)
{
    int max_x, max_y;
    struct sdl2_state *scon = get_scon_from_window(ev->key.windowID);

    if (qemu_input_is_absolute() || absolute_enabled) {
        int scr_w, scr_h;
        SDL_GetWindowSize(scon->real_window, &scr_w, &scr_h);
        max_x = scr_w - 1;
        max_y = scr_h - 1;
        if (gui_grab && (ev->motion.x == 0 || ev->motion.y == 0 ||
                         ev->motion.x == max_x || ev->motion.y == max_y)) {
            sdl_grab_end(scon);
        }
        if (!gui_grab &&
            (ev->motion.x > 0 && ev->motion.x < max_x &&
             ev->motion.y > 0 && ev->motion.y < max_y)) {
            sdl_grab_start(scon);
        }
    }
    if (gui_grab || qemu_input_is_absolute() || absolute_enabled) {
        sdl_send_mouse_event(scon, ev->motion.xrel, ev->motion.yrel, 0,
                             ev->motion.x, ev->motion.y, ev->motion.state);
    }
}

static void handle_mousebutton(SDL_Event *ev)
{
    int buttonstate = SDL_GetMouseState(NULL, NULL);
    SDL_MouseButtonEvent *bev;
    struct sdl2_state *scon = get_scon_from_window(ev->key.windowID);
    int dz;

    bev = &ev->button;
    if (!gui_grab && !qemu_input_is_absolute()) {
        if (ev->type == SDL_MOUSEBUTTONUP && bev->button == SDL_BUTTON_LEFT) {
            /* start grabbing all events */
            sdl_grab_start(scon);
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
        sdl_send_mouse_event(scon, 0, 0, dz, bev->x, bev->y, buttonstate);
    }
}

static void handle_windowevent(DisplayChangeListener *dcl, SDL_Event *ev)
{
    int w, h;
    struct sdl2_state *scon = get_scon_from_window(ev->key.windowID);

    switch (ev->window.event) {
    case SDL_WINDOWEVENT_RESIZED:
        sdl_scale(scon, ev->window.data1, ev->window.data2);
        graphic_hw_invalidate(scon->dcl.con);
        graphic_hw_update(scon->dcl.con);
        break;
    case SDL_WINDOWEVENT_EXPOSED:
        SDL_GetWindowSize(SDL_GetWindowFromID(ev->window.windowID), &w, &h);
        sdl_update(dcl, 0, 0, w, h);
        break;
    case SDL_WINDOWEVENT_FOCUS_GAINED:
    case SDL_WINDOWEVENT_ENTER:
        if (!gui_grab && (qemu_input_is_absolute() || absolute_enabled)) {
            absolute_mouse_grab(scon);
        }
        break;
    case SDL_WINDOWEVENT_FOCUS_LOST:
        if (gui_grab && !gui_fullscreen) {
            sdl_grab_end(scon);
        }
        break;
    case SDL_WINDOWEVENT_RESTORED:
        update_displaychangelistener(dcl, GUI_REFRESH_INTERVAL_DEFAULT);
        break;
    case SDL_WINDOWEVENT_MINIMIZED:
        update_displaychangelistener(dcl, 500);
        break;
    case SDL_WINDOWEVENT_CLOSE:
        if (!no_quit) {
            no_shutdown = 0;
            qemu_system_shutdown_request();
        }
        break;
    }
}

static void sdl_refresh(DisplayChangeListener *dcl)
{
    struct sdl2_state *scon = container_of(dcl, struct sdl2_state, dcl);
    SDL_Event ev1, *ev = &ev1;

    if (scon->last_vm_running != runstate_is_running()) {
        scon->last_vm_running = runstate_is_running();
        sdl_update_caption(scon);
    }

    graphic_hw_update(dcl->con);

    while (SDL_PollEvent(ev)) {
        switch (ev->type) {
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
        case SDL_WINDOWEVENT:
            handle_windowevent(dcl, ev);
            break;
        default:
            break;
        }
    }
}

static void sdl_mouse_warp(DisplayChangeListener *dcl,
                           int x, int y, int on)
{
    struct sdl2_state *scon = container_of(dcl, struct sdl2_state, dcl);
    if (on) {
        if (!guest_cursor) {
            sdl_show_cursor();
        }
        if (gui_grab || qemu_input_is_absolute() || absolute_enabled) {
            SDL_SetCursor(guest_sprite);
            if (!qemu_input_is_absolute() && !absolute_enabled) {
                SDL_WarpMouseInWindow(scon->real_window, x, y);
            }
        }
    } else if (gui_grab) {
        sdl_hide_cursor();
    }
    guest_cursor = on;
    guest_x = x, guest_y = y;
}

static void sdl_mouse_define(DisplayChangeListener *dcl,
                             QEMUCursor *c)
{

    if (guest_sprite) {
        SDL_FreeCursor(guest_sprite);
    }

    if (guest_sprite_surface) {
        SDL_FreeSurface(guest_sprite_surface);
    }

    guest_sprite_surface =
        SDL_CreateRGBSurfaceFrom(c->data, c->width, c->height, 32, c->width * 4,
                                 0xff0000, 0x00ff00, 0xff, 0xff000000);

    if (!guest_sprite_surface) {
        fprintf(stderr, "Failed to make rgb surface from %p\n", c);
        return;
    }
    guest_sprite = SDL_CreateColorCursor(guest_sprite_surface,
                                         c->hot_x, c->hot_y);
    if (!guest_sprite) {
        fprintf(stderr, "Failed to make color cursor from %p\n", c);
        return;
    }
    if (guest_cursor &&
        (gui_grab || qemu_input_is_absolute() || absolute_enabled)) {
        SDL_SetCursor(guest_sprite);
    }
}

static void sdl_cleanup(void)
{
    if (guest_sprite) {
        SDL_FreeCursor(guest_sprite);
    }
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
    char *filename;
    int i;

    if (no_frame) {
        gui_noframe = 1;
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

    flags = SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE;
    if (SDL_Init(flags)) {
        fprintf(stderr, "Could not initialize SDL(%s) - exiting\n",
                SDL_GetError());
        exit(1);
    }

    for (i = 0;; i++) {
        QemuConsole *con = qemu_console_lookup_by_index(i);
        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }
    }
    sdl2_num_outputs = i;
    sdl2_console = g_new0(struct sdl2_state, sdl2_num_outputs);
    for (i = 0; i < sdl2_num_outputs; i++) {
        QemuConsole *con = qemu_console_lookup_by_index(i);
        sdl2_console[i].dcl.ops = &dcl_ops;
        sdl2_console[i].dcl.con = con;
        register_displaychangelistener(&sdl2_console[i].dcl);
        sdl2_console[i].idx = i;
    }

    /* Load a 32x32x4 image. White pixels are transparent. */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, "qemu-icon.bmp");
    if (filename) {
        SDL_Surface *image = SDL_LoadBMP(filename);
        if (image) {
            uint32_t colorkey = SDL_MapRGB(image->format, 255, 255, 255);
            SDL_SetColorKey(image, SDL_TRUE, colorkey);
            SDL_SetWindowIcon(sdl2_console[0].real_window, image);
        }
        g_free(filename);
    }

    if (full_screen) {
        gui_fullscreen = 1;
        sdl_grab_start(0);
    }

    mouse_mode_notifier.notify = sdl_mouse_mode_change;
    qemu_add_mouse_mode_change_notifier(&mouse_mode_notifier);

    gui_grab = 0;

    sdl_cursor_hidden = SDL_CreateCursor(&data, &data, 8, 1, 0, 0);
    sdl_cursor_normal = SDL_GetCursor();

    atexit(sdl_cleanup);
}
#endif
