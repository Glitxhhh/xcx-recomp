// Runs the real text window and callbacks in headless ImGui frames, with only controller/focus sampling replaced.
#include "../runtime/ui/menu.cpp"

static uint64_t test_buttons;
static int32_t test_x, test_y;
static int test_connected = 1, test_focus = 1;
extern "C" int __wrap_input_pad_read(uint64_t *b, int32_t *x, int32_t *y) {
    *b = test_buttons; *x = test_x; *y = test_y;
    return test_connected;
}
extern "C" int __wrap_ikb_foreground(void) { return test_focus; }

static int test_failures, test_checks;
static void check(bool yes, const char *name) {
    ++test_checks;
    if (!yes) { ++test_failures; printf("FAIL: %s (text='%s')\n", name, g_text_buf); }
}
static int tick() {
    ImGuiIO &io = ImGui::GetIO();
    int reset = g_reset.exchange(0);
    if (reset) { io.ClearInputKeys(); io.ClearInputMouse(); ImGui::ClearActiveID(); }
    drain_events(io);
    int pad = text_pad(reset);
    ImGui::NewFrame();
    int answer = layout_text(1280, 720, 0.8f, pad);
    ImGui::Render();
    return answer;
}
static void begin(const char *initial, int max = 10, int min = 1, unsigned disabled = 0, int numeric = 0) {
    test_buttons = test_x = test_y = 0;
    test_connected = test_focus = 1;
    menu_text_begin("Name test", initial, max, min, disabled, numeric);
    tick(); tick(); tick();
}
static int tap(int bit) {
    test_buttons = 1ull << bit;
    int a = tick();
    test_buttons = 0; tick();
    return a;
}
static int key(int row, int col) {
    g_text_row = row; g_text_col = col;
    return tap(B_A);
}
static int physical(ImGuiKey k) {
    ImGui::GetIO().AddKeyEvent(k, true); int a = tick();
    ImGui::GetIO().AddKeyEvent(k, false); tick();
    return a;
}
static void type(const char *s) { ImGui::GetIO().AddInputCharactersUTF8(s); tick(); tick(); }

extern "C" int controller_keyboard_checks(void) {
    make_context();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720); io.DeltaTime = 1.0f / 60;
    unsigned char *pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    begin("Cross");
    check(key(1, 0) == 0 && !strcmp(g_text_buf, "q"), "A replaces selected initial text without confirming");
    key(1, 1); check(!strcmp(g_text_buf, "qw"), "second controller character appends");
    key(2, 0); key(2, 1); check(!strcmp(g_text_buf, "qwA"), "case preserves existing cursor and inserts uppercase");
    key(0, 0); key(4, 0); key(0, 10);
    check(!strcmp(g_text_buf, "qwA1"), "digits, space and backspace");
    tap(B_X); check(!strcmp(g_text_buf, "qwA"), "configured X shortcut deletes once");
    tap(B_ZL); key(1, 0); check(!strcmp(g_text_buf, "qwAq"), "left trigger toggles caps lock");
    check(key(5, 0) == 1, "grid OK confirms in one activation");
    check(key(5, 1) == -1, "grid Cancel cancels");
    check(tap(B_PLUS) == 1 && tap(B_B) == -1, "Plus confirms and B cancels");
    begin("", 10, 0); g_text_row = 2; g_text_col = 1; tap(B_DOWN);
    check(g_text_row == 3 && g_text_col == 1, "vertical navigation follows nearest letter below A");
    g_text_row = 4; g_text_col = 0; tap(B_DOWN);
    check(g_text_row == 5 && g_text_col == 0, "space navigates down to separate OK row");
    begin("", 10, 0); key(3, 0); key(1, 0); key(1, 1);
    check(!strcmp(g_text_buf, "Qw"), "on-screen Shift applies to one character");
    begin("", 10, 0);
    test_buttons = (1ull << B_ZR); tick();
    test_buttons |= 1ull << B_A; tick();
    test_buttons = 0; tick(); key(1, 1);
    check(!strcmp(g_text_buf, "Qw"), "right trigger shifts only while held");
    tap(B_ZL); key(1, 0); key(1, 1);
    check(!strcmp(g_text_buf, "QwQW"), "caps lock persists across characters");
    test_buttons = 1ull << B_ZR; tick(); test_buttons |= 1ull << B_A; tick(); test_buttons = 0; tick();
    check(!strcmp(g_text_buf, "QwQWw"), "held shift reverses caps lock");
    begin("", 10, 0); type("a漢é");
    tap(B_L); tap(B_Y); check(!strcmp(g_text_buf, "a漢"), "bumper left and forward delete handle UTF-8");
    tap(B_L); tap(B_X); check(!strcmp(g_text_buf, "漢"), "backspace deletes behind cursor");
    tap(B_R); key(0, 0); check(!strcmp(g_text_buf, "漢1"), "right bumper advances a whole UTF-8 character");
    tap(B_R); tap(B_Y); check(!strcmp(g_text_buf, "漢1"), "forward delete at end does nothing");
    begin("Selected"); tap(B_Y); check(!g_text_buf[0], "forward delete removes selection");
    begin("Selected"); tap(B_L); key(0, 0); check(!strcmp(g_text_buf, "1Selected"), "left bumper collapses selection to start");
    begin("Selected"); tap(B_R); key(0, 0); check(!strcmp(g_text_buf, "Selected1"), "right bumper collapses selection to end");
    begin("", 10, 0); for (int i = 0; i < 14; i++) key(1, 0);
    check(!strcmp(g_text_buf, "qqqqqqqqqq"), "controller ten-unit limit");
    begin("", 10, 1); check(key(5, 0) == 0 && tap(B_PLUS) == 0, "minimum length blocks both confirms");
    begin("Initial"); type("É漢"); check(!strcmp(g_text_buf, "É漢"), "physical Unicode replaces selected initial text");
    physical(ImGuiKey_End); key(0, 10); check(!strcmp(g_text_buf, "É"), "controller backspace removes a complete UTF-8 character");
    type("z"); check(!strcmp(g_text_buf, "Éz"), "physical typing continues after controller editing");
    physical(ImGuiKey_Home); key(0, 1); check(!strcmp(g_text_buf, "2Éz"), "grid insertion uses physical text cursor");
    begin("Reopened"); check(!strcmp(g_text_buf, "Reopened"), "new request loads supplied initial text");
    check(key(1, 0) == 0 && !strcmp(g_text_buf, "q"), "reopened text is selected for replacement");
    begin("Keep", 10, 1, 1u << 1); key(4, 0); check(!strcmp(g_text_buf, "Keep"), "disabled grid character preserves selection");
    begin("", 10, 0, 0, 1); key(1, 0); key(0, 2); check(!strcmp(g_text_buf, "3"), "numeric validation shared by grid");
    begin("Cross"); test_buttons = 1ull << B_A; tick(); tick(); tick();
    check(!strcmp(g_text_buf, "q"), "held activation inserts once"); test_buttons = 0; tick();
    menu_text_begin("Name test", "Held", 10, 1, 0, 0);
    test_buttons = 1ull << B_A; tick(); tick();
    check(!strcmp(g_text_buf, "Held"), "held opening A is gated");
    test_buttons = 0; tick(); tap(B_A); check(!strcmp(g_text_buf, "q"), "opening release rearms activation");
    begin("Focus"); test_focus = 0; test_buttons = 1ull << B_A; tick(); test_focus = 1; tick();
    check(!strcmp(g_text_buf, "Focus"), "focus regain gates held activation");
    test_buttons = 0; tick(); tap(B_A); check(!strcmp(g_text_buf, "q"), "activation recovers after focus release");
    begin("Device"); test_connected = 0; tick(); test_buttons = 1ull << B_A; test_connected = 1; tick();
    check(!strcmp(g_text_buf, "Device"), "reconnect gates held activation");
    test_buttons = 0; tick(); tap(B_A); check(!strcmp(g_text_buf, "q"), "reconnect release recovers activation");
    begin("Drift"); test_x = 12000; tick(); tick(); check(g_text_col == 0, "small stick motion does not navigate");
    test_x = 20000; tick(); check(g_text_col == 1, "intentional stick motion navigates");
    test_x = 0; tick();
    test_buttons = 1ull << B_RIGHT; tick(); int col = g_text_col;
    tick(); check(g_text_col == col, "navigation waits before repeating");
    plat_sleep_ms(360); tick(); check(g_text_col == col + 1, "held direction repeats after delay");
    test_buttons = 0; tick();
    begin("Mouse");
    text_activate('q'); ImGui::ClearActiveID(); tick(); tick();
    text_activate('w'); ImGui::ClearActiveID(); tick(); tick();
    check(!strcmp(g_text_buf, "qw"), "grid refocus preserves text cursor and selection");
    begin("Mouse");
    ImGuiWindow *window = ImGui::FindWindowByName("##text");
    const ImGuiStyle &style = ImGui::GetStyle();
    float field_x = window->DC.CursorStartPos.x + style.FramePadding.x;
    float field_y = window->DC.CursorStartPos.y + 2 * ImGui::GetFontSize() + 3 * style.ItemSpacing.y;
    auto click = [&](float x, float y) {
        menu_event_mouse_pos(x, y); tick();
        menu_event_mouse_button(0, 1); tick();
        menu_event_mouse_button(0, 0); tick(); tick(); tick(); tick(); tick();
    };
    click(field_x, field_y + ImGui::GetFrameHeight() * 0.5f);
    type("K");
    check(!strcmp(g_text_buf, "KMouse"), "mouse places insertion cursor in physical text field");
    float grid_y = field_y + ImGui::GetFrameHeight() + 2 * style.ItemSpacing.y;
    float gap = style.ItemSpacing.x * 0.4f;
    float width = window->Size.x - 2 * style.WindowPadding.x;
    float cell = (width - 11 * gap) / 12;
    click(window->DC.CursorStartPos.x + (cell + gap) * 0.5f + cell * 0.5f,
          grid_y + 46 * 0.8f + gap + 23 * 0.8f);
    check(!strcmp(g_text_buf, "KqMouse"), "mouse-operated grid preserves field insertion cursor");
    begin("", 3, 0); type("é漢Ab");
    check(!strcmp(g_text_buf, "é漢A"), "physical multibyte text is limited by UTF-16 units, not bytes");
    begin("", 10, 0, (1u << 2) | (1u << 3)); type("a@b%c");
    check(!strcmp(g_text_buf, "abc"), "physical input uses disabled-character validation");
    begin("🐴Ab", 3, 0);
    check(!strcmp(g_text_buf, "🐴A"), "initial supplementary character counts as two UTF-16 units");
    tap(B_R); tap(B_X); tap(B_X);
    check(!g_text_buf[0], "backspace removes complete initial supplementary character");
    begin("", 10, 1);
    check(physical(ImGuiKey_Enter) == 0, "physical Enter honors minimum length");
    menu_text_begin("Name test", "Held", 10, 1, 0, 0);
    test_buttons = 1ull << B_PLUS;
    check(tick() == 0 && tick() == 0, "held opening confirm is gated");
    test_buttons = 0; tick();
    check(tap(B_PLUS) == 1, "confirm recovers after opening release");
    check(physical(ImGuiKey_Enter) == 1, "physical Enter confirms");
    check(physical(ImGuiKey_Escape) == -1, "physical Escape cancels");
    printf("controller-keyboard: %d checks, %d failures\n", test_checks, test_failures);
    ImGui::DestroyContext(); return test_failures != 0;
}
