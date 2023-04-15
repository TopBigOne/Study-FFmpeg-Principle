#include <stdio.h>

#include <SDL2/SDL.h>

const int WIDTH = 400, HEIGHT = 400;


void my_code();

int main() {
    my_code();
    return 0;
}

void my_code() {
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {

        return;
    }

    SDL_Window *sdlWindow = SDL_CreateWindow(
            "筱雅，你好",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            WIDTH,
            HEIGHT,
            SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (sdlWindow == NULL) {
        perror(" the window is NULL.");
        return;
    }
    SDL_Event window_event;
    while (1) {
        if (SDL_PollEvent(&window_event)) {
            if (window_event.type == SDL_QUIT) {
                puts("SDL quit.");
                break;
            }
        }
    }

    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();


}
