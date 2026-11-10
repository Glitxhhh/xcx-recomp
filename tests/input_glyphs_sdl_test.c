// Test-only replacement main; exercise actual SDL adapter event filtering without creating a window.
#include <assert.h>
#include <stdio.h>
#include "../runtime/input_sdl.c"
int __wrap_main(int argc,char **argv) {
    (void)argc;(void)argv;
    SDL_Event e={0};focused=1;captured=1;
    const SDL_MouseID generated[]={SDL_TOUCH_MOUSEID,SDL_PEN_MOUSEID};
    for (unsigned i=0;i<2;i++) {
        e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.which=generated[i];
        input_sdl_event(&e);assert(ikb_down(KEY_MOUSE_L)&&!ikb_intent_down(KEY_MOUSE_L));
        e.type=SDL_EVENT_MOUSE_BUTTON_UP;input_sdl_event(&e);assert(!ikb_down(KEY_MOUSE_L));
        e.type=SDL_EVENT_MOUSE_MOTION;e.motion.which=generated[i];e.motion.xrel=20;e.motion.yrel=5;
        input_sdl_event(&e);
        int32_t x,y;ikb_mouse_take(&x,&y);assert(x==20&&y==5);
        ikb_intent_mouse_take(&x,&y);assert(!x&&!y);
    }
    e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.which=1;
    input_sdl_event(&e);assert(ikb_down(KEY_MOUSE_L)&&ikb_intent_down(KEY_MOUSE_L));
    e.type=SDL_EVENT_MOUSE_MOTION;e.motion.which=1;e.motion.xrel=20;e.motion.yrel=5;input_sdl_event(&e);
    int32_t x,y;ikb_intent_mouse_take(&x,&y);assert(x==20&&y==5);ikb_mouse_take(&x,&y);assert(x==20&&y==5);
    e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_R;input_sdl_event(&e);assert(ikb_down(KEY_R)&&ikb_intent_down(KEY_R));
    assert(ikb_glyph_key(KEY_TAB)==KEY_TAB&&ikb_glyph_key(KEY_LCTRL)==KEY_LCTRL&&ikb_glyph_key(KEY_LSHIFT)==KEY_LSHIFT&&ikb_glyph_key(KEY_SPACE)==KEY_SPACE);
    assert(ikb_glyph_key(KEY_UP)==KEY_UP&&ikb_glyph_key(KEY_DOWN)==KEY_DOWN&&ikb_glyph_key(KEY_LEFT)==KEY_LEFT&&ikb_glyph_key(KEY_RIGHT)==KEY_RIGHT);
    e.type=SDL_EVENT_WINDOW_FOCUS_LOST;input_sdl_event(&e);assert(!ikb_down(KEY_R)&&!ikb_intent_down(KEY_R)&&!ikb_intent_down(KEY_MOUSE_L));
    puts("SDL adapter: generated touch/pen clicks and motion excluded from intent; gameplay states/deltas preserved; genuine input and focus clearing passed");
    return 0;
}
