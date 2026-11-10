// The software keyboard's front end for SDL windows (swkbd.h): a text field on the menu overlay (ui/menu.h,
// menu_text_begin) over the game's last frame. The game waits for the answer and presents nothing meanwhile, so this
// thread presents that frame again about 60 times a second (vk_present_repeat) until the field answers. SDL text input is
// on while it is up, allowing physical text input and a system keyboard where supported. The controller grid works
// without a system keyboard: A selects a key, Plus or OK confirms, and B or Cancel cancels. Enter / Esc also work. XCX asks for one name, the player character's after the avatar is
// made. Before the game has presented anything (the --probe-services cases) there is no frame to draw over: the
// starting text is taken at once, and XCX_SWKBD_TEST=cancel stands for Cancel pressed.
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "input_sdl.h"
#include "platform/platform.h"
#include "swkbd.h"
#include "ui/menu.h"
#include "vk.h"

static size_t units(const uint16_t *s) {
    size_t k = 0;
    while (s[k]) k++;
    return k;
}

int text_entry_run(const SwkbdRequest *req, uint16_t *out, size_t cap, size_t *n) {
    if (!vk_present_repeat()) {   // no frame to draw over
        const char *test = cfg_get("XCX_SWKBD_TEST");
        if (test && !strcmp(test, "cancel")) { *n = 0; return 0; }
        size_t k = units(req->initial);
        if (k > cap) k = cap;
        memcpy(out, req->initial, k * 2);
        *n = k;
        rt_log("swkbd: nothing presented yet: the %s name is taken", req->game_initial ? "game's" : "default");
        return k > 0;
    }
    static char prompt[0x101 * 3], text[(KB_LIMIT + 1) * 3];   // one entry at a time: the game's thread waits in it
    swkbd_utf16_to_utf8(req->prompt, units(req->prompt), prompt, sizeof prompt);
    swkbd_utf16_to_utf8(req->initial, units(req->initial), text, sizeof text);
    menu_text_begin(prompt, text, req->max_len, req->min_len, req->disabled, req->numeric);
    gfx_window_sdl_text_input(1);
    rt_log("swkbd: text entry on the overlay");
    int answer;
    while (!(answer = menu_text_poll(text, sizeof text))) {
        plat_sleep_ms(15);
        vk_present_repeat();
    }
    vk_present_repeat();   // the frame without the entry, until the game presents again
    gfx_window_sdl_text_input(0);
    rt_log("swkbd: %s", answer > 0 ? "OK" : "cancelled");
    if (answer < 0) {
        *n = 0;
        return 0;
    }
    *n = swkbd_utf8_to_utf16(text, out, cap);
    return 1;
}
