#ifndef SDL2_H
#define SDL2_H

struct sdl2_console {
    DisplayChangeListener dcl;
    DisplaySurface *surface;
    SDL_Texture *texture;
    SDL_Window *real_window;
    SDL_Renderer *real_renderer;
    int idx;
    int last_vm_running; /* per console for caption reasons */
    int x, y;
    int hidden;
};

#endif /* SDL2_H */
