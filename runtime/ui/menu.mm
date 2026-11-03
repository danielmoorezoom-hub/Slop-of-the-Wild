// In-game overlay menu built with Dear ImGui (Metal renderer on macOS, Vulkan elsewhere; input fed from SDL events).
//
// Threads: SDL events arrive on the main thread and are queued; the menu is laid out and drawn on the
// render thread inside backend_present, after the game image, into the same drawable.
#ifdef __APPLE__
#import <Metal/Metal.h>
#endif
#include <ctype.h>
#include <SDL3/SDL.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "i18n.h"
#include "imgui.h"
#include "option_rows.h"
#include "saves_tab.h"
#include "imgui_internal.h"  // (NavCursorVisible)
#include "osk.h"
#ifdef __APPLE__
#include "imgui_impl_metal.h"
#else
#include "imgui_impl_vulkan.h"
extern "C" bool vk_imgui_backend_init(void);  // runtime/nvn/vulkan/imgui_glue.cpp
extern "C" void rec_drain(void);              // runtime/nvn/vulkan/record.c
#endif

extern "C" {
#include "../hle.h"
#include "../benchmark.h"
void photo_toggle(void);
void photo_shot(void);
void photo_filter_next(void);
#include "../teleport.h"
#include "../settings.h"
#include "../color_grade.h"
#include "../window_size.h"
extern "C" {
#include "../language.h"
}
#include "menu.h"
void input_reset(void);
void input_pc_escape(bool down);
void controls_bindings_draw(float scale);  // controls_ui.mm
#include "../input_bind.h"
#include "../perfmon.h"
#include "achievements_ui.h"  // (the Achievements page and the unlock toasts)
#include "../paths.h"
#include "../pc_cursor.h"
}

// ------------------------------------------------------------------ state shared with the main thread
static std::atomic<bool> g_open{false};
static std::atomic<bool> g_text_busy{false};  // a text field / the on-screen keyboard has the keys (render thread: ui_text_busy)
static std::atomic<bool> g_want_window_apply{false};  // fullscreen or the window size changed
// (also from the game's Options screen, game_options.mm)
extern "C" void menu_request_window_apply(void) { g_want_window_apply.store(true); }
// The window size stepped through (left / right): applied once it stays put a moment, not at every size on the way
static std::atomic<uint64_t> g_size_apply_at{0};  // SDL_GetTicks() then (0: none)
extern "C" void menu_request_window_size_apply(void) { g_size_apply_at.store(SDL_GetTicks() + 600); }
extern "C" bool game_options_input_hold(void);  // game_options.mm: a binding capture from the game's Options screen

struct QEvent {
    enum { KEY, ANALOG, MOUSE_POS, MOUSE_BUTTON, WHEEL, TEXT, FOCUS } kind;
    ImGuiKey key;
    bool down;
    float value, x, y;
    int button;
    char text[32];
};
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static std::vector<QEvent> q_events;
static float g_pixel_density = 1;

static void push(const QEvent &e) {
    pthread_mutex_lock(&q_lock);
    q_events.push_back(e);
    pthread_mutex_unlock(&q_lock);
}

bool menu_is_open(void) { return g_open.load(); }
extern "C" const char *rt_slop_version(void);  // (runtime/ui/launcher.mm)
static const char *slop_title(void) {
    static char t[48];
    if (!t[0]) snprintf(t, sizeof t, "Slop of the Wild %s", rt_slop_version());
    return t;
}
static std::atomic<uint64_t> g_title_tick{0};  // SDL_GetTicks() of the title menu's last update (game thread)
extern "C" void menu_title_tick(void) { g_title_tick.store(SDL_GetTicks()); }
extern "C" bool menu_title_visible(void) {  // (vulkan/device.c: the title art capture)
    uint64_t tt = g_title_tick.load();
    return tt && SDL_GetTicks() - tt < 300 && !g_open.load();
}
static void set_open(bool open);
extern "C" void menu_test_open(void) {
    set_open(true);
    if (getenv("SAVAGEWIND_MENU_TAB")) { extern int g_tab_request_ext; g_tab_request_ext = atoi(getenv("SAVAGEWIND_MENU_TAB")); }
}

void menu_tabs_focus(bool first_tab);
static float g_open_a;  // the menu's fade / rise in (0..1)
// Slop of the Wild: the game keeps running under the overlay (it only loses the controls), so opening it in play
// presses the game's Plus - its pause menu - and closing it presses Plus again. Not on the title screen (the overlay
// stands alone there) and not while a movie plays (Plus asks to skip it).
extern "C" volatile int g_inject_plus, g_movie_playing;  // runtime/hle/input.c, movie.c
static bool g_paused_game;
static void set_open(bool open) {
    if (open == g_open.load()) return;
    if (open) g_open_a = 0;
    {
        uint64_t tt = g_title_tick.load();
        bool title = tt && SDL_GetTicks() - tt < 1000;
        if (open && !title && !g_movie_playing && !getenv("SAVAGEWIND_MENU_NO_PAUSE")) g_inject_plus = 1, g_paused_game = true;
        else if (!open && g_paused_game) g_inject_plus = 1, g_paused_game = false;
    }
    g_open.store(open);
    input_reset();  // nothing held while the menu had focus leaks into the game
    bind_capture_cancel();
    if (open) {
        static bool opened;  // the controller's cursor: the first tab (Controls) the first time this session, later the
        menu_tabs_focus(!opened);  // selected one
        opened = true;
    }
    else { settings_save(); osk_cancel(); g_text_busy = false; }
}

static ImGuiKey key_from_scancode(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_LGUI: return ImGuiKey_LeftSuper;
    case SDL_SCANCODE_RGUI: return ImGuiKey_RightSuper;
    case SDL_SCANCODE_A: return ImGuiKey_A;
    case SDL_SCANCODE_C: return ImGuiKey_C;
    case SDL_SCANCODE_V: return ImGuiKey_V;
    case SDL_SCANCODE_X: return ImGuiKey_X;
    case SDL_SCANCODE_Z: return ImGuiKey_Z;
    case SDL_SCANCODE_Q: return ImGuiKey_Q;  // previous tab
    case SDL_SCANCODE_E: return ImGuiKey_E;  // next tab
    default: return ImGuiKey_None;
    }
}

// Nintendo layout: the right face button (A) confirms, the bottom one (B) goes back.
static ImGuiKey key_from_pad(Uint8 b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceDown;   // activate
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceRight; // cancel / back
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

// ---- software keyboard (runtime/hle/swkbd.c: the game's name entry etc.) --------------------------------------
// The game thread waits in menu_keyboard_run (re-presenting the last frame); this dialog takes the text: typed
// directly when the player last used a keyboard, an on-screen keyboard (d-pad + A, B deletes, Start / + OK) when
// they last used a controller.
static std::atomic<int> g_kbd{0};  // 1 open, 2 confirmed, 3 cancelled
static std::atomic<bool> g_last_pad{false};
static char g_kbd_text[96];
static int g_kbd_max = 8;
// Window size's list over the game's Options screen (game_options.mm A_SIZE_LIST): up, and until when the press that
// closed it is kept from the game
static std::atomic<bool> g_sizes{false};
static std::atomic<uint64_t> g_sizes_hold{0};  // SDL_GetTicks()
extern "C" void game_options_size_picked(void);  // game_options.mm (the row shows the new size)
extern "C" void menu_size_list_open(void) {
    input_reset();
    g_sizes = true;
}
static bool capturing() { return g_open.load() || g_kbd.load() == 1 || g_sizes.load(); }
// The game gets no input while this is true (runtime/hle/input.c): menu or name entry up, or a binding capture begun
// from the game's Options screen.
extern "C" bool menu_input_captured(void) {
    return capturing() || game_options_input_hold() || SDL_GetTicks() < g_sizes_hold.load();
}

// Esc opens the game's pause screen or backs out of a menu. F2 frees the mouse cursor; a click in the
// window holds it again (runtime/hle/input.c).
static bool overlay_enabled(void) {
    // Slop of the Wild: this menu is the controller's: Home (PS, Xbox) or Back+Start opens it (SAVAGEWIND_OVERLAY=0: off)
    static const bool on = !(getenv("SAVAGEWIND_OVERLAY") && *getenv("SAVAGEWIND_OVERLAY") == '0');
    return on;
}
static std::atomic<bool> g_cursor_free{false};
extern "C" bool menu_cursor_free(void) { return g_cursor_free.load(); }

bool menu_handle_event(const SDL_Event *e) {
    static bool back_down, start_down;
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST || e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        back_down = start_down = false;
        QEvent q = {};
        q.kind = QEvent::FOCUS;
        q.down = e->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        push(q);
        return false;  // the input runtime also releases its cached controls
    }
    // a binding capture (Controls tab, or the game's Options screen) takes the press
    if (bind_capture_event(e)) return true;
    if (e->type == SDL_EVENT_KEY_DOWN) g_last_pad = false;
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) g_last_pad = true;
    if (g_kbd.load() == 1 && e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {  // Enter confirms, Esc cancels
        if (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER) { g_kbd = 2; return true; }
        if (e->key.scancode == SDL_SCANCODE_ESCAPE) { g_kbd = 3; return true; }
    }
    if (g_kbd.load() == 1 && e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
        g_kbd = 2;
        return true;
    }
    // typing (a text field, the on-screen keyboard, the name entry): keys are text, not menu shortcuts
    bool typing = g_kbd.load() == 1 || (g_open.load() && g_text_busy.load());
    if (e->type == SDL_EVENT_KEY_UP && e->key.scancode == SDL_SCANCODE_ESCAPE)
        input_pc_escape(false);
    if (pc_cursor_enabled() && !capturing() && !typing && !game_options_input_hold() &&
        (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) && e->key.scancode == SDL_SCANCODE_ESCAPE) {
        if (!e->key.repeat) input_pc_escape(e->type == SDL_EVENT_KEY_DOWN);
        return true;
    }
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && g_cursor_free.load() && !capturing() && !pc_cursor_active()) {
        g_cursor_free = false;  // (the click only takes the cursor back)
        return true;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
        if (!e->key.repeat && pc_cursor_enabled() && e->key.scancode == SDL_SCANCODE_F2 && !typing) {
            g_cursor_free = !g_cursor_free.load();
            return true;
        }
        if (!e->key.repeat && !pc_cursor_enabled() && e->key.scancode == SDL_SCANCODE_ESCAPE && !g_open.load()) {  // (menu open: closes it, below)
            if (!typing) g_cursor_free = !g_cursor_free.load();
            return true;
        }
        if (!e->key.repeat && e->key.scancode == SDL_SCANCODE_F3 && !typing && overlay_enabled()) {  // this menu from the keyboard
            set_open(!g_open.load());
            return true;
        }
        if (!e->key.repeat && e->key.scancode == SDL_SCANCODE_F6 && !typing) {  // photo mode (runtime/photo.c)
            photo_toggle();
            return true;
        }
        if (!e->key.repeat && e->key.scancode == SDL_SCANCODE_F8 && !typing) {
            photo_filter_next();
            return true;
        }
        if (!e->key.repeat && e->key.scancode == SDL_SCANCODE_F7 && !typing) {
            photo_shot();
            return true;
        }
        if (!e->key.repeat && e->key.scancode == SDL_SCANCODE_F10 && !typing) {  // performance overlay: off, FPS, full
            g_settings.perf_overlay = (g_settings.perf_overlay + 1) % 3;
            settings_save();
            return true;
        }
        if (!e->key.repeat && (e->key.scancode == SDL_SCANCODE_F11 ||
                               (e->key.scancode == SDL_SCANCODE_RETURN && (e->key.mod & (SDL_KMOD_ALT | SDL_KMOD_GUI))) ||
                               (e->key.scancode == SDL_SCANCODE_F && (e->key.mod & SDL_KMOD_GUI) && (e->key.mod & SDL_KMOD_CTRL)))) {
            g_settings.fullscreen = !g_settings.fullscreen;
            g_want_window_apply.store(true);
            settings_save();
            return true;
        }
        /* fallthrough */
    case SDL_EVENT_KEY_UP: {
        if (!capturing()) return false;
        bool down = e->type == SDL_EVENT_KEY_DOWN;
        if (down && e->key.scancode == SDL_SCANCODE_ESCAPE) {  // Esc: back out of popups, else close
            QEvent q = {};
            q.kind = QEvent::KEY; q.key = ImGuiKey_Escape; q.down = true; push(q);
            q.down = false; push(q);
            return true;
        }
        ImGuiKey k = key_from_scancode(e->key.scancode);
        if (g_sizes.load()) {  // Window size's list (in game): the keys bound to A, B and up / down work it too
            int sc = e->key.scancode;
            if (sc == g_bind_key[BIND_A]) k = ImGuiKey_Enter;
            else if (sc == g_bind_key[BIND_B]) k = ImGuiKey_Escape;
            else if (sc == g_bind_key[BIND_LS_UP] || sc == g_bind_key[BIND_UP]) k = ImGuiKey_UpArrow;
            else if (sc == g_bind_key[BIND_LS_DOWN] || sc == g_bind_key[BIND_DOWN]) k = ImGuiKey_DownArrow;
        }
        if (k != ImGuiKey_None) { QEvent q = {}; q.kind = QEvent::KEY; q.key = k; q.down = down; push(q); }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT:
        if (!capturing()) return false;
        { QEvent q = {}; q.kind = QEvent::TEXT; strlcpy(q.text, e->text.text, sizeof q.text); push(q); }
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        bool down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) back_down = down;
        if (e->gbutton.button == SDL_GAMEPAD_BUTTON_START) start_down = down;
        if (down && !typing && overlay_enabled() &&
            ((back_down && start_down) || e->gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)) {
            set_open(!g_open.load());
            return true;
        }
        if (!capturing()) return false;
        ImGuiKey k = key_from_pad(e->gbutton.button);
        if (g_sizes.load()) {  // Window size's list (in game): the buttons bound to A and B, wherever they are
            int b = e->gbutton.button;
            if (b == g_bind_pad[BIND_A]) k = ImGuiKey_GamepadFaceDown;
            else if (b == g_bind_pad[BIND_B]) k = ImGuiKey_GamepadFaceRight;
        }
        if (k != ImGuiKey_None) { QEvent q = {}; q.kind = QEvent::KEY; q.key = k; q.down = down; push(q); }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        if (!capturing()) return false;
        float v = e->gaxis.value / 32767.0f;
        QEvent q = {};
        q.kind = QEvent::ANALOG;
        switch (e->gaxis.axis) {
        case SDL_GAMEPAD_AXIS_LEFTX:
            q.key = ImGuiKey_GamepadLStickLeft; q.value = v < -0.3f ? -v : 0; push(q);
            q.key = ImGuiKey_GamepadLStickRight; q.value = v > 0.3f ? v : 0; push(q);
            break;
        case SDL_GAMEPAD_AXIS_LEFTY:
            q.key = ImGuiKey_GamepadLStickUp; q.value = v < -0.3f ? -v : 0; push(q);
            q.key = ImGuiKey_GamepadLStickDown; q.value = v > 0.3f ? v : 0; push(q);
            break;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: q.key = ImGuiKey_GamepadL2; q.value = v; push(q); break;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: q.key = ImGuiKey_GamepadR2; q.value = v; push(q); break;
        default: break;
        }
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (!capturing()) return false;
        { QEvent q = {}; q.kind = QEvent::MOUSE_POS; q.x = e->motion.x * g_pixel_density; q.y = e->motion.y * g_pixel_density; push(q); }
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!capturing()) return false;
        {
            QEvent q = {};
            q.kind = QEvent::MOUSE_BUTTON;
            q.button = e->button.button == SDL_BUTTON_LEFT ? 0 : e->button.button == SDL_BUTTON_RIGHT ? 1 : 2;
            q.down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            push(q);
        }
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!capturing()) return false;
        { QEvent q = {}; q.kind = QEvent::WHEEL; q.x = e->wheel.x; q.y = e->wheel.y; push(q); }
        return true;
    default:
        return false;
    }
}

void menu_apply_window_requests(void *win) {
    SDL_Window *w = (SDL_Window *)win;
    g_pixel_density = SDL_GetWindowPixelDensity(w);
    static bool text_input;
    bool want_text = capturing();
    if (want_text != text_input) {
        if (want_text) SDL_StartTextInput(w);
        else SDL_StopTextInput(w);
        text_input = want_text;
    }
    // (the window was made at its size and fullscreen mode: ui_window_init)
    uint64_t at = g_size_apply_at.load();
    if (at && SDL_GetTicks() >= at && g_size_apply_at.compare_exchange_strong(at, 0)) g_want_window_apply.store(true);
    if (g_want_window_apply.exchange(false)) winsize_apply(w);
    else winsize_refresh(w);  // (moved to another display, or its desktop size changed: other sizes are offered)
}

// ------------------------------------------------------------------ render thread
static bool g_inited;
static ImGuiContext *g_ctx;  // ours: the start screen (back to it after a reboot) has its own context
static ImFont *g_font, *g_font_big;
static ImFont *g_menu_font, *g_menu_font_big;  // the menu window (Fira Sans); everything else keeps g_font
static int g_tab, g_tab_request = -2;  // (-2: no request; -1 is the Prelaunch tab)
static int g_focus_tab = -99;  // put the nav cursor on this tab next frame (-100: the selected one; -99: none)
static int g_focus_frames;

// The start screen's Settings opening: the controller's cursor starts on the first tab the first time in a session,
// later on the tab that's selected
void menu_tabs_focus(bool first_tab) { g_focus_tab = first_tab ? -1 : -100, g_focus_frames = 0; }
int g_tab_request_ext = -1;

static void style_init(void) {  // Slop of the Wild: Sheikah slate - deep blue-black glass, cyan glow, amber for changes
    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 18; s.FrameRounding = 6; s.GrabRounding = 6; s.TabRounding = 6; s.ChildRounding = 10;
    s.WindowPadding = ImVec2(32, 26); s.FramePadding = ImVec2(16, 9); s.ItemSpacing = ImVec2(14, 12);
    s.WindowBorderSize = 1; s.ScrollbarSize = 12; s.ScrollbarRounding = 6; s.TabBarBorderSize = 0;
    ImVec4 *c = s.Colors;
    ImVec4 cyan(0.745f, 0.886f, 0.33f, 1), cyan_dim(0.20f, 0.55f, 0.62f, 1), deep(0.05f, 0.11f, 0.16f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.045f, 0.07f, 0.90f);
    c[ImGuiCol_Border] = ImVec4(0.745f, 0.886f, 0.33f, 0.22f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.03f, 0.06f, 0.09f, 0.98f);
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.95f, 0.96f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.56f, 0.60f, 1);
    c[ImGuiCol_FrameBg] = deep;
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.25f, 0.14f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.33f, 0.16f, 1);
    c[ImGuiCol_Button] = ImVec4(0.07f, 0.15f, 0.12f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.33f, 0.16f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.42f, 0.50f, 1);
    c[ImGuiCol_Header] = ImVec4(0.15f, 0.25f, 0.14f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.33f, 0.16f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(0.14f, 0.42f, 0.50f, 1);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.33f, 0.16f, 0.8f);
    c[ImGuiCol_TabSelected] = ImVec4(0.15f, 0.25f, 0.14f, 1);
    c[ImGuiCol_TabSelectedOverline] = cyan;
    c[ImGuiCol_CheckMark] = cyan;
    c[ImGuiCol_SliderGrab] = cyan_dim;
    c[ImGuiCol_SliderGrabActive] = cyan;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.745f, 0.886f, 0.33f, 0.25f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.745f, 0.886f, 0.33f, 0.45f);
    c[ImGuiCol_ScrollbarGrabActive] = cyan;
    c[ImGuiCol_NavCursor] = cyan;
    c[ImGuiCol_Separator] = ImVec4(0.745f, 0.886f, 0.33f, 0.18f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_WindowBg];
}


// The fonts, with the menu language's glyphs that Latin fonts lack merged in (runtime/ui/i18n.mm); made again when the
// menu language changes (fonts_follow_language).
static int g_fonts_lang = -1;
static void load_fonts(void) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    g_font = g_font_big = g_menu_font = g_menu_font_big = NULL;
    g_fonts_lang = ui_lang_generation();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    auto add = [&](const char *path, float size, ImFontConfig c) {
        ImFont *f = io.Fonts->AddFontFromFileTTF(path, size, &c);
        if (f) ui_merge_fallback_font(io.Fonts, size, &cfg);
        return f;
    };
    const char *paths[] = {"/System/Library/Fonts/Avenir Next.ttc", "/System/Library/Fonts/Helvetica.ttc",
                           "C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                           "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"};
    for (const char *p : paths) {
        if (FILE *f = fopen(p, "rb")) fclose(f);  // a missing file is an assertion in ImGui, not a NULL return
        else continue;
        if (!g_font) g_font = add(p, 34, cfg);
        if (g_font && !g_font_big) { ImFontConfig b = cfg; b.FontNo = 0; g_font_big = add(p, 56, b); }
    }
    // the menu window itself: Fira Sans, as on the start screen (shipped, OFL) - Medium for the rows, SemiBold for the
    // titles - at its Settings window's sizes; the FPS counter, overlays and on-screen keyboard keep the fonts above
    char med[1024], semi[1024];
    const char *test = getenv("SAVAGEWIND_MENU_FONT");  // (test) another weight / face
    if (test) strlcpy(med, test, sizeof med), strlcpy(semi, test, sizeof semi);
    if (test || (rt_resource("FiraSans-Medium.ttf", "third_party/fonts/FiraSans/FiraSans-Medium.ttf", med, sizeof med) &&
                 rt_resource("FiraSans-SemiBold.ttf", "third_party/fonts/FiraSans/FiraSans-SemiBold.ttf", semi, sizeof semi))) {
        g_menu_font = add(med, 29, cfg);
        g_menu_font_big = add(semi, 53.5f, cfg);
    }
    // the achievement toasts' title: a book serif in bold (Palatino, Georgia, DejaVu Serif), else Fira's SemiBold
    ImFont *toast_title = NULL;
    const char *serif[] = {"C:/Windows/Fonts/palab.ttf", "C:/Windows/Fonts/georgiab.ttf",
                           "/System/Library/Fonts/Supplemental/Palatino.ttc",
                           "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
                           "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSerif-Bold.ttf"};
    for (const char *p : serif) {
        if (FILE *f = fopen(p, "rb")) fclose(f);
        else continue;
        if ((toast_title = add(p, 56, cfg))) break;
    }
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    if (!g_font_big) g_font_big = g_font;
    if (!g_menu_font || !g_menu_font_big) g_menu_font = g_font, g_menu_font_big = g_font_big;
    ach_ui_fonts(toast_title ? toast_title : g_menu_font_big, g_menu_font);
}

// Before a frame: another menu language may need other glyphs - the fonts and their texture are made again
extern "C" void vk_imgui_fonts_changed(void);  // (runtime/nvn/vulkan/imgui_glue.cpp)
static void fonts_follow_language(void) {
    if (g_fonts_lang == ui_lang_generation()) return;
    load_fonts();
#ifdef __APPLE__
    ImGui_ImplMetal_DestroyFontsTexture();  // (NewFrame makes it again)
#else
    vk_imgui_fonts_changed();
#endif
}

#ifdef __APPLE__
static void init(id<MTLDevice> dev) {
#else
static void init(void) {
#endif
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.IniFilename = NULL;
    load_fonts();
    style_init();
#ifdef __APPLE__
    ImGui_ImplMetal_Init(dev);
#else
    vk_imgui_backend_init();
#endif
    g_inited = true;
}

static void drain_events(ImGuiIO &io) {
    pthread_mutex_lock(&q_lock);
    std::vector<QEvent> ev;
    ev.swap(q_events);
    pthread_mutex_unlock(&q_lock);
    for (const QEvent &q : ev) {
        switch (q.kind) {
        case QEvent::KEY: io.AddKeyEvent(q.key, q.down); break;
        case QEvent::ANALOG: io.AddKeyAnalogEvent(q.key, q.value > 0.1f, q.value); break;
        case QEvent::MOUSE_POS: io.AddMousePosEvent(q.x, q.y); break;
        case QEvent::MOUSE_BUTTON: io.AddMouseButtonEvent(q.button, q.down); break;
        case QEvent::WHEEL: io.AddMouseWheelEvent(q.x, q.y); break;
        case QEvent::TEXT: io.AddInputCharactersUTF8(q.text); break;
        case QEvent::FOCUS: io.AddFocusEvent(q.down); break;
        }
    }
}

// Section header in the big font.
static void header(const char *t) {  // (Slop of the Wild: a section title as on the other tabs - cyan, upper case)
    std::string u = tr_upper(tr(t));
    ImGui::TextColored(ImVec4(0.745f, 0.886f, 0.33f, 1), "%s", u.c_str());
    ImGui::Separator();
    ImGui::Spacing();
}

static void hint(const char *t) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tr(t));
    ImGui::PopTextWrapPos();
}

// A row with a label on the left and a choice of options on the right (dpad left/right cycles).
// reserve: room kept free right of the combo (a button on the same line)
static bool choice(const char *label, int *value, const char *const *names, int n, float reserve = 0) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr(label));
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
    bool changed = false;
    ImGui::SetNextItemWidth(reserve > 0 ? -reserve : -1);
    if (ImGui::BeginCombo("##c", tr(names[*value]))) {
        for (int i = 0; i < n; i++) {
            ImGui::PushID(i);
            if (ImGui::Selectable(tr(names[i]), i == *value)) { *value = i; changed = true; }
            ImGui::PopID();
            if (i == *value) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemFocused() && !ImGui::IsPopupOpen("##c")) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            *value = (*value + 1) % n; changed = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            *value = (*value + n - 1) % n; changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

static bool toggle(const char *label, bool *v) {
    const char *names[] = {"Off", "On"};
    int i = *v;
    bool ch = choice(label, &i, names, 2);
    *v = i;
    return ch;
}

extern "C" void renderer_get_scale(float *sx, float *sy);
extern "C" void renderer_get_scene_scale(float *sx, float *sy);
extern "C" bool renderer_gpu_rtx(void);  // (vulkan/device.c, metal.m)
// Video > Upscaler mode's names, by UPQ_*
static const char *const k_upscale_modes[UPQ_COUNT] = {"Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance"};
extern "C" {
#include "../nvn/output_scale.h"
}

#ifndef __APPLE__
// Colour (runtime/color_grade.h; vulkan/exec.c scene_post), the Video tab's last section: a preset sets the sliders,
// which then show "Custom" when they match none
static void tab_colour(void) {
    opt_section("Colour");
    {
        static std::string names_buf[16];
        const char *names[16];
        bool avail[16];
        int n = g_color_preset_count;
        for (int i = 0; i <= n && i < 16; i++) {
            names_buf[i] = tr(i < n ? g_color_presets[i].name : "Custom");
            names[i] = names_buf[i].c_str();
            avail[i] = i < n;
        }
        static int shown = -1;
        int v = color_preset_current();
        if (v < 0) v = n;  // (custom: shown, never picked)
        if (opt_choice_avail("Colour preset", &v, &shown, names, avail, n + 1, 0,
                             "Ready-made looks for the 3D image, made from the sliders below: Natural is the game's "
                             "own colours, Warm and Cool shift the light, Vivid and Cinematic add contrast, Muted "
                             "calms the colours. The HUD and menus keep their colours.") && v < n)
            color_preset_apply(v);
    }
    float ev = g_settings.color_exposure / 10.0f;
    if (opt_slider("Exposure", &ev, -2, 2, 0.1f, 0, "%+.1f EV",
                   "Makes the 3D image brighter or darker, in stops: +1 EV is twice as bright."))
        g_settings.color_exposure = (int)lroundf(ev * 10);
    opt_int("Contrast", &g_settings.color_contrast, -50, 50, 5, 0, "%+d",
            "More (or less) difference between dark and bright areas. Shadows and highlights keep their detail.");
    opt_int("Saturation", &g_settings.color_saturation, -100, 100, 5, 0, "%+d",
            "How strong the colours are. -100: black and white.");
    opt_int("Temperature", &g_settings.color_temperature, -100, 100, 5, 0, "%+d",
            "The colour of the light: below 0 cooler (bluer), above 0 warmer (more orange).");
    opt_int("Tint", &g_settings.color_tint, -100, 100, 5, 0, "%+d",
            "Below 0 toward green, above 0 toward magenta: evens out a colour cast.");
    {   // LUT: None, the shipped ones, the player's own (listed again every couple of seconds while shown)
        static int n;
        static uint64_t at;
        if (!n || SDL_GetTicks() - at > 2000) n = color_lut_list(), at = SDL_GetTicks();
        static std::string none;
        none = tr("None");
        const char *names[COLOR_LUT_MAX];
        for (int i = 0; i < n; i++) names[i] = i ? color_lut_name(i) : none.c_str();
        int cur = color_lut_index(g_settings.color_lut);
        char desc[1500];
        snprintf(desc, sizeof desc,
                 tr("A look-up table (LUT) that recolours the 3D image. Savage Wind's own are listed first. Your own LUTs "
                    "go in this folder: .cube files (up to 64 x 64 x 64) and ReShade-style .png strips (1024 x 32, "
                    "4096 x 64). They are listed by file name.\n\n%s"), color_lut_folder());
        bool picked;
        if (opt_list("LUT", &cur, names, n, 0, desc, &picked))
            snprintf(g_settings.color_lut, sizeof g_settings.color_lut, "%s", color_lut_id(cur));
    }
    opt_indent(1);
    if (!g_settings.color_lut[0]) opt_disable_next();
    opt_int("LUT intensity", &g_settings.color_lut_strength, 0, 100, 5, 100, "%d%%",
            "How much of the LUT's look is used: 100% is all of it, 0% none.");
    opt_indent(-1);
}
#endif

static void tab_display(void) {
    float scale = ImGui::GetFontSize() / 34;
    opt_begin("##video", scale);
    opt_section("Display");
    bool fs = g_settings.fullscreen;
    if (opt_toggle("Fullscreen", &fs, true, "Fills the screen. Also: F11, Alt+Enter or Cmd+Ctrl+F.")) {
        g_settings.fullscreen = fs;
        g_want_window_apply.store(true);
    }
    {   // Fullscreen mode (settings.h FSMODE_*; runtime/window_size.c winsize_apply)
        static const char *const names[FSMODE_COUNT] = {"Automatic", "Borderless", "Exclusive"};
        int cur = g_settings.fullscreen_mode;
        if (opt_choice("Fullscreen mode (beta)", &cur, names, FSMODE_COUNT, FSMODE_AUTO,
                       "Automatic: borderless at your display's own size, the display switched to the size chosen at "
                       "any other.\n\nBorderless: never switches the display; another size is scaled to the screen. "
                       "Quick to switch away from.\n\nExclusive: the display switched to the size chosen, at its "
                       "highest refresh rate, its own size too.")) {
            g_settings.fullscreen_mode = cur;
            if (g_settings.fullscreen) g_want_window_apply.store(true);
        }
    }
    {   // Monitor (runtime/window_size.c): Automatic, then the displays connected (and the one set, while it isn't)
        WinDisplay ds[WINDISPLAY_MAX];
        static char names_buf[WINDISPLAY_MAX + 2][96];
        const char *names[WINDISPLAY_MAX + 2];
        int n = windisplay_list(ds), cur = windisplay_target(), gone = -1;
        names[0] = "Automatic";
        for (int i = 0; i < n; i++) {
            windisplay_name(&ds[i], i + 1, tr("primary"), names_buf[i + 1], sizeof names_buf[i + 1]);
            names[i + 1] = names_buf[i + 1];
        }
        if (g_settings.display > 0 && !cur) {
            snprintf(names_buf[n + 1], sizeof names_buf[n + 1], tr("%s (not connected)"), g_settings.display_name);
            names[n + 1] = names_buf[n + 1];
            cur = gone = n + 1;
        }
        int pick = cur;
        if (opt_choice("Monitor (beta)", &pick, names, n + 1 + (gone >= 0), 0,
                       "The monitor the game is shown on, in a window or fullscreen. Automatic: where the window is "
                       "(the primary monitor at start). A monitor that isn't connected is Automatic until it is.") &&
            pick != gone) {
            windisplay_set(pick);
            g_want_window_apply.store(true);
        }
    }
    {   // Window size (runtime/window_size.c): the sizes the display can show, its own first, then grouped by shape
        WinSize list[WINSIZE_MAX];
        static char names_buf[WINSIZE_MAX][64];
        const char *names[WINSIZE_MAX];
        int n = winsize_list(list, g_settings.fullscreen), cur = 0, def = 0;
        WinSize e = winsize_effective(g_settings.fullscreen), d = winsize_default(g_settings.fullscreen);
        WinSize desk = winsize_desktop();
        for (int i = 0; i < n; i++) {
            winsize_name(list[i], tr("native"), names_buf[i], sizeof names_buf[i]);
            names[i] = names_buf[i];
            if (list[i].w == e.w && list[i].h == e.h) cur = i;
            if (list[i].w == d.w && list[i].h == d.h) def = i;
        }
        char desc[600];
        snprintf(desc, sizeof desc, tr("The size of the game's window, or of the screen in fullscreen. Only the sizes your "
                                       "display can show: its own first, then grouped by shape. Any shape other than 16:9 "
                                       "is filled with Fill wide screens on; off, it keeps 16:9 with bars.\n\nYour display: %d x %d."), desk.w, desk.h);
        bool picked;
        if (opt_list("Window size", &cur, names, n, def, desc, &picked)) {  // (past either end: the other one)
            g_settings.window_w = list[cur].w, g_settings.window_h = list[cur].h;
            if (picked) g_want_window_apply.store(true);
            else menu_request_window_size_apply();  // (stepping: the size where it stops)
        }
    }
    {   // Fill wide screens (runtime/nvn/output_scale.c rt_persp)
        bool wv = g_settings.wide_view;
        if (opt_toggle("Fill wide screens (beta)", &wv, true,
                       "On a window or screen that isn't 16:9 the game shows more of the world instead of black bars: "
                       "to the sides when it's wider (21:9, 32:9), above and below when it's taller (16:10, 4:3). "
                       "The HUD stays 16:9 in the middle; the pause menu, the map and the start screen are shown "
                       "16:9."))
            g_settings.wide_view = wv;
    }
    {   // Field of view (runtime/nvn/output_scale.c rt_persp)
        static const int fov[] = FOV_CHOICES;
        enum { NV = sizeof fov / sizeof fov[0] };
        static char names_buf[NV][16];
        const char *names[NV];
        int cur = 0, def = 0;
        for (int i = 0; i < NV; i++) {
            snprintf(names_buf[i], sizeof names_buf[i], "%d%%", fov[i]);
            names[i] = names_buf[i];
            if (fov[i] == g_settings.fov) cur = i;
            if (fov[i] == 100) def = i;
        }
        if (opt_choice("Field of view", &cur, names, NV, def,
                       "How much of the world the camera shows in play: 100% is the game's own. Cutscenes and "
                       "conversations keep theirs.", false))
            g_settings.fov = fov[cur];
    }
    {   // Frame rate (runtime/hle/framepace.c)
        static const int fps[] = FPS_CHOICES;
        enum { NF = sizeof fps / sizeof fps[0] };
        static char names_buf[NF][32];
        const char *names[NF];
        int cur = 0, def = 0;
        for (int i = 0; i < NF; i++) {
            if (fps[i]) snprintf(names_buf[i], sizeof names_buf[i], tr("%d FPS"), fps[i]);
            names[i] = fps[i] ? names_buf[i] : "Off";
            if (fps[i] == g_settings.fps_limit) cur = i;
            if (fps[i] == FPS_DEFAULT) def = i;
        }
        std::string desc = tr("How many pictures a second the game draws. The game itself runs at the same speed at "
                              "every rate (the console runs it at 30). Higher rates need a faster computer. Menus, "
                              "cutscenes and conversations use it too, and videos play at their own speed. Off: as "
                              "many as the computer can draw.");
        int hz = (int)lroundf(winsize_refresh_rate());
        if (hz > 0) {
            char line[200];
            snprintf(line, sizeof line, tr("Your display refreshes %d times a second: a rate above that shows no more "
                                           "pictures, and one that divides it evenly is the smoothest."), hz);
            desc += std::string("\n\n") + line;
        }
        if (opt_choice("Frame rate", &cur, names, NF, def, desc.c_str(), false)) g_settings.fps_limit = fps[cur];
    }
    {   // VSync (settings.h VSYNC_*: runtime/nvn/vulkan/device.c, runtime/hle/framepace.c)
        static const char *const names[VSYNC_COUNT] = {"On", "Fast", "Adaptive", "Off"};
        std::string desc = tr("On: no tearing; the game waits for the display. With G-SYNC or FreeSync the display "
                              "waits for the game instead: each frame shows as soon as it's ready, and the frame rate "
                              "stays a little under the display's refresh rate. Fast: no tearing and no waiting: each "
                              "refresh shows the newest frame. Adaptive: as On, but a frame that comes late shows at "
                              "once and may tear. Off: each frame shows at once: the least delay, but it tears.");
        float hz = winsize_refresh_rate();
        if (hz >= 30) {
            char line[200];
            snprintf(line, sizeof line,
                     tr("With On and Adaptive, the frame rate on this display goes up to about %d FPS."),
                     (int)lroundf(hz - hz * hz / 3600));
            desc += std::string("\n\n") + line;
        }
        opt_choice("VSync", &g_settings.vsync, names, VSYNC_COUNT, VSYNC_DEFAULT, desc.c_str(), false);
    }
    {   // HDR (settings.h HDR_*: runtime/nvn/vulkan/device.c hdr_wanted)
        static const char *const names[HDR_COUNT] = {"Automatic", "On", "Off"};
        opt_choice("HDR", &g_settings.hdr, names, HDR_COUNT, HDR_AUTO,
                   "Automatic: while Windows HDR is on for this display, the picture is sent in HDR, so it looks as bright "
                   "and colourful as in SDR (in SDR, Windows shows it dim and dull then). On: always in HDR, where the "
                   "display takes it. Off: always SDR. White follows Windows' SDR content brightness (Settings > System > "
                   "Display > HDR).", false);
    }
    static const char *const perf_names[] = {"Off", "FPS", "Full"};
    opt_choice("Performance overlay", &g_settings.perf_overlay, perf_names, 3, PERF_OFF,
               "FPS: the frame rate and frame time. Full: also CPU, memory and GPU load and temperatures, a "
               "frame time graph and Link's coordinates. F10 switches between them while you play.");

    opt_section("Start");
    opt_toggle("Start at Continue (beta)", &g_settings.auto_continue, false, "Beta, may break things. Goes past the title screen by itself, into your latest save. Press any button to stop it.");
    opt_section("Profiles");
    for (int i = 0; i < 3; i++) {
        char l[32];
        snprintf(l, sizeof l, "Load profile %d (beta)", i + 1);
        if (!settings_profile_exists(i)) opt_disable_next();
        if (opt_action(l, settings_profile_exists(i) ? "" : tr("Empty"), tr("Every setting as you saved it."), false)) settings_profile_load(i);
        snprintf(l, sizeof l, "Save settings as profile %d (beta)", i + 1);
        if (opt_action(l, "", tr("Keeps all your settings under this number."), false)) settings_profile_save(i);
    }
    opt_section("Saves");
    opt_toggle("Back up saves when the game starts", &g_settings.save_backups, true, "Keeps the last 5 copies in save-backups, if the saves have changed.");
    opt_section("Benchmark");
    for (int i = 0; i < benchmark_count(); i++) {  // (runtime/benchmark.c: the game restarts for it)
        std::string d = std::string(tr(benchmark_about(i))) + " " +
                        tr("The game restarts, walks the route by itself from a copy of your save, and comes back to the start "
                           "screen. Your save isn't touched, but unsaved progress is lost.");
        if (!benchmark_can_run()) d = tr("Needs a save to start from: play until the game has saved, then try again.");
        std::string l = std::string(tr("Benchmark:")) + " " + benchmark_name(i);
        if (!benchmark_can_run()) opt_disable_next();
        if (opt_action(l.c_str(), *benchmark_last(i) ? benchmark_last(i) : tr("Run"), d.c_str(), false)) benchmark_run(i);
    }
    opt_section("Rendering");
    {   // Graphics preset (settings.h): sets the rows it names; "Custom" is shown, never picked
        static const char *const gp[] = {"Potato", "Low", "Medium", "High", "Custom"};
        static const bool gpa[] = {true, true, true, true, false};
        int cur = settings_preset_current(), shown = cur;
        if (opt_choice_avail("Graphics preset", &cur, &shown, gp, gpa, 5, GRAPHICS_PRESET_COUNT,
                             "Sets anti-aliasing, texture filtering, render scale and shadow resolution together. Potato: the "
                             "fastest. High: the sharpest, for a strong graphics card. Change any of them below and it shows Custom."))
            settings_preset_apply(cur);
    }
#ifdef __APPLE__  // (Slop of the Wild: render scale supersamples instead; settings.c keeps the upscaler off elsewhere)
    {   // Upscaler: Off, this OS's spatial upscaler (its modes on the row below), and the temporal ones still to come.
        // Values that can't be picked yet are shown greyed ("coming soon"); stepping onto them keeps the one in use.
        // The modes wait for render-target scaling (upscale_modes_available): until then the upscaler brings the
        // game's own 1920 x 1080 image to a bigger window.
#ifdef __APPLE__
        const char *ups[] = {"Off", "MetalFX"};
        static const int ups_value[] = {UPSCALE_OFF, UPSCALE_METALFX};
        const char *ud = "How the game's image (1920 x 1080) is scaled up to a bigger window. Off: plain (bilinear) "
                         "scaling. MetalFX: Apple's upscaler, sharper.\n\nRendering below the window's size (the modes "
                         "below) is coming in a later version.";
#endif
        enum { NU = sizeof ups_value / sizeof ups_value[0] };
        bool avail[NU];
        int ui = 0;
        for (int i = 0; i < NU; i++) {
            avail[i] = upscaler_available(ups_value[i]);
            if (ups_value[i] == g_settings.upscaler) ui = i;
        }
        static int shown = -1;
        int udef = 0;  // (the reset value: UPSCALE_DEFAULT)
        for (int i = 0; i < NU; i++) if (ups_value[i] == UPSCALE_DEFAULT) udef = i;
        if (opt_choice_avail("Upscaler", &ui, &shown, ups, avail, NU, udef, ud)) g_settings.upscaler = ups_value[ui];
        // its mode (AMD's scale factors), one step in; greyed when the upscaler is off
        const char *const *modes = k_upscale_modes;
        bool modes_ok = upscale_modes_available();
        std::string mode_names[UPQ_COUNT];
        const char *mn[UPQ_COUNT];
        bool mavail[UPQ_COUNT];
        for (int i = 0; i < UPQ_COUNT; i++) {
            mode_names[i] = modes_ok ? tr(modes[i]) : std::string(tr(modes[i])) + " " + tr("(coming soon)");
            mn[i] = mode_names[i].c_str();
            mavail[i] = modes_ok;
        }
        float sx, sy;  // the 3D scene's size (BOTW docked: 1600 x 900 times its scale)
        renderer_get_scene_scale(&sx, &sy);
        char rendering[100];
        snprintf(rendering, sizeof rendering, tr("Rendering at %.0f x %.0f."), 1600.0 * sx, 900.0 * sy);
        std::string md = std::string(tr("How much smaller the game renders before the upscaler brings it to the window, "
                                        "on each side: Ultra Quality 1.3x, Quality 1.5x, Balanced 1.7x, Performance "
                                        "2x, Ultra Performance 3x. Smaller is faster and softer.")) +
                         "\n\n" + (modes_ok ? rendering : tr("Coming in a later version."));
        opt_indent(1);
        if (g_settings.upscaler == UPSCALE_OFF) opt_disable_next();
        static int mshown = -1;
        opt_choice_avail("Upscaler mode", &g_settings.upscale_quality, &mshown, mn, mavail, UPQ_QUALITY + 1, UPQ_QUALITY,
                         md.c_str());
        opt_indent(-1);
    }
#endif
#ifdef __APPLE__
    static const char *aa[] = {"Off", "SMAA"};
    opt_choice("Anti-aliasing", &g_settings.antialias, aa, 2, AA_SMAA,
               "Smooths jagged edges of the game image before it is scaled to the window. SMAA: finds the edges and "
               "blends only along them, so textures stay sharp. Most useful with an upscaler.");
#else
    {   // (the TAA sentence on its own: the first one keeps its translations)
        static const char *aa[] = {"Off", "SMAA", "TAA", "AMD FSR 4 (beta)"};  // (settings.h AA_*)
        static std::string d;
        d = std::string(tr("Smooths jagged edges of the game image before it is scaled to the window. SMAA: finds the "
                           "edges and blends only along them, so textures stay sharp. Most useful with an upscaler.")) +
            " " + tr("TAA: blends each frame with the ones before it, following the game's motion, so edges stay "
                     "smooth and stop shimmering; slightly softer, use it with Sharpening.");
        opt_choice("Anti-aliasing", &g_settings.antialias, aa, 4, AA_SMAA, d.c_str());
    }
#endif
#ifndef __APPLE__
    {   // Anisotropic filtering and sharpening (Vulkan: vulkan/textures.c sampler_aniso, vulkan/exec.c scene_post)
        static const char *const aniso[] = {"Off", "2x", "4x", "8x", "16x"};
        opt_choice("Anisotropic filtering", &g_settings.anisotropy, aniso, 5, 4,
                   "Keeps the textures of ground, paths and walls sharp where you look along them into the distance. "
                   "Off: the game's own filtering, as on the console. 16x is the sharpest and costs very little on a PC "
                   "graphics card. Render targets keep the game's own filtering (no dark crust on edges).", false);
        static const char *const clar[] = {"Off", "Serfrost", "Serfrost (dim display)", "Serfrost (bright display)"};
        opt_toggle("Wet grass fix (beta)", &g_settings.wet_fix, true, "Tames the white glare on grass in rain and sun. Applies from the next start. Beta, may break things.");
        opt_choice("Clarity", &g_settings.clarity, clar, 4, 1,
                   "Cemu's Clarity for Breath of the Wild (graphic packs, presets by Serfrost) in place of the game's own "
                   "tone mapping: less bloom and haze, deeper blacks, richer color. Takes effect the next time the game "
                   "starts.", false);
        static const char *const rscale[] = {"100%", "125%", "150%", "200%"};
        opt_choice("Render scale", &g_settings.render_scale, rscale, 4, 0,
                   "Supersampling: above 100% the game renders at a higher resolution than the window and is scaled "
                   "down to it, the cleanest way to smooth edges and shimmer. 200% is 4x the pixels: for fast "
                   "graphics cards.", false);
        opt_toggle("AMD fast path", &g_settings.fast_path, true,
                   "Lets the game read and write the same image in one pass (Vulkan attachment feedback loops) instead "
                   "of copying it first. About 1.6x the frame rate on AMD Radeon. Off only to rule it out if something "
                   "draws wrong. Takes effect the next time the game starts.");
    }
#endif
    opt_toggle("Asynchronous shaders", &g_settings.async_shaders, true,
               "Off: the game waits for each new shader to compile, a short stutter the first time an effect "
               "appears, and everything is drawn. On: no waiting, but what needs a shader still compiling is skipped "
               "meanwhile: effects pop in late, and pictures the game draws only once (the save file's screenshot) "
               "can come out as garbage.");
    // shadow resolution (its own block)
    static const char *shadow[] = {"Console (896)", "High (2048)", "Ultra (4096)", "Extreme (8192)"};
    opt_choice("Shadow resolution", &g_settings.shadow_res, shadow, SHADOW_COUNT, SHADOW_CONSOLE,
               "The size of the maps the sun's shadows are drawn from. Console: the game's own, 896 x 896. High (2048), "
               "Ultra (4096) and Extreme (8192) give sharper shadow edges (Link, trees, buildings) for more GPU time "
               "and video memory (Ultra: about 100 MB, Extreme: about 400 MB).");
    {   // the menus' one-channel textures enlarged (runtime/nvn/text_upscale.c): read when the game starts
        static const char *menus[] = {"Off", "2x", "4x"};
        int cur = g_settings.sharper_menus / 2;
        if (opt_choice("Sharper menus (beta)", &cur, menus, 3, 0,
                       "Makes the shapes and frames of the game's menus sharper on 1440p and 4K screens. It takes effect the next "
                       "time you start the game. This is an upscale made on your computer from the game's own art, not "
                       "Nintendo's original art.", false))
            g_settings.sharper_menus = cur * 2;
    }
    {   // the game's font sheets enlarged (runtime/nvn/text_upscale.c): read when the game starts
        static const char *text[] = {"Off", "2x", "4x"};
        int cur = g_settings.sharper_text / 2;
        if (opt_choice("Sharper text (beta)", &cur, text, 3, 0,
                       "Makes the game's letters sharper on 1440p and 4K screens. It takes effect the next time you start "
                       "the game. This is an upscale made on your computer from the game's own letters, not Nintendo's "
                       "original art: the shapes are kept, but they aren't exactly as drawn. 4x takes a moment longer to "
                       "start and uses more video memory.", false))
            g_settings.sharper_text = cur * 2;
    }
#ifndef __APPLE__
    tab_colour();
#endif
    opt_end();
}

static void tab_distance(void) {
    opt_begin("##distance", ImGui::GetFontSize() / 34);
    opt_section("Extended draw distance");
    opt_toggle("Extended draw distance (beta)", &g_settings.lod_extended, false,
               "Draws grass, ground cover, trees and objects farther away, with the sliders below. Off: as on the console. Costs frame "
               "rate: try the Benchmark on the Video page. Grass reaches farthest if this is on when the game starts. Beta, may break things.");
    if (g_settings.lod_extended) {
        float master = (float)g_settings.lod_master;
        if (opt_slider("Overall distance (beta)", &master, 0, 100, 5, 75, "%g%%",
                       "Moves all three sliders below together, from the game's own at 0% to the farthest at 100%. Fine-tune them "
                       "afterwards. Beta, may break things."))
            settings_lod_master_apply((int)master);
        opt_toggle("Hold the frame rate (beta)", &g_settings.lod_auto, false,
                   "Pulls the grass in when the game can't keep up with your frame rate limit, and lets it back out when it can. Beta, may break things.");
        opt_section("Fine-tune");
        float dist = (float)g_settings.lod_distance, grass = (float)g_settings.lod_grass, objects = (float)g_settings.lod_objects;
        if (opt_slider("Draw distance (beta)", &dist, 100, 150, 10, 140, "%g%%",
                       "How far the distant trees are drawn. 100% is the game's own. Beta, may break things."))
            g_settings.lod_distance = (int)dist;
        if (opt_slider("Grass distance (beta)", &grass, 100, 250, 10, 210, "%g%%",
                       "How far grass reaches. 100% is the game's own. Beta, may break things."))
            g_settings.lod_grass = (int)grass;
        if (opt_slider("Object distance (beta)", &objects, 100, 140, 10, 130, "%g%%",
                       "How far objects and enemies are shown and kept active. 100% is the game's own. Higher: enemies can "
                       "notice you from farther away, and it costs more frame rate. Beta, may break things."))
            g_settings.lod_objects = (int)objects;
    }
    opt_end();
}

static void tab_cheats(void) {
    opt_begin("##cheats", ImGui::GetFontSize() / 34);
    opt_section("Cheats (beta): may break things");
    {
        static const char *wx[] = {"Automatic", "No rain", "No thunderstorms", "Clear", "Cloudy", "Rain", "Heavy rain",
                                   "Snow", "Heavy snow", "Thunderstorm", "Thunder rain", "Rain in sunshine"};
        opt_choice("Weather (beta)", &g_settings.weather, wx, 12, 0, "Holds the weather you pick, or keeps out rain or thunderstorms. Cutscenes keep theirs. Beta, may break things.");
    }
    opt_toggle("Infinite health (beta)", &g_settings.cheat_health, false, "You lose no hearts and can't die. Healing still works.");
    opt_toggle("Infinite stamina (beta)", &g_settings.cheat_stamina, false, "The stamina wheel doesn't go down. It still fills.");
    opt_toggle("Infinite arrows (beta)", &g_settings.cheat_arrows, false, "Shooting doesn't use up arrows.");
    opt_toggle("Unbreakable weapons (beta)", &g_settings.cheat_weapons, false, "Weapons, bows and shields lose no durability and never break.");
    opt_toggle("One-hit kills (beta)", &g_settings.cheat_onehit, false, "Your hits finish any enemy.");
    {
        float dd = (float)g_settings.damage_dealt;
        if (opt_slider("Damage dealt (beta)", &dd, 25, 1000, 25, 100, "%g%%", "How much your hits hurt enemies. 100% is the game's own."))
            g_settings.damage_dealt = (int)dd;
    }
    {
        float dmg = (float)g_settings.damage_taken;
        if (opt_slider("Damage taken (beta)", &dmg, 25, 300, 25, 100, "%g%%", "How much the hits you take hurt. 100% is the game's own."))
            g_settings.damage_taken = (int)dmg;
    }
    opt_toggle("Infinite rupees (beta)", &g_settings.cheat_rupees, false, "Keeps you at 9999 rupees, within about 5 seconds.");
    {
        float wear = (float)g_settings.weapon_wear;
        if (opt_slider("Weapon wear (beta)", &wear, 10, 300, 10, 100, "%g%%", "How fast weapons, bows and shields wear out. 100% is the game's own."))
            g_settings.weapon_wear = (int)wear;
    }
    opt_end();
}


// Items: adds items to Link's pouch through the game's own pick-up call (runtime/spawner.c). In game, 1.9.0 only.
extern "C" {
int spawner_available(void);
const char *spawner_status(void);
int spawner_request(const char *name, int value, int count, int stack);
int spawner_remove_request(const char *name, int count);
int spawner_request_mod(const char *name, int value, int count, int stack, uint32_t mod_flags, int32_t mod_value);
void spawner_tweak_set(int which, float mult);
float spawner_tweak_get(int which);
int spawner_tweak_found(void);
int spawner_tweak_total(void);
int spawner_armor_state(void);
typedef struct { char id[64]; int cat, life; } ModItem;
int spawner_mod_items(const ModItem **out);
typedef struct { char name[64]; int type, value, equipped; } InvItem;
int spawner_inventory(InvItem *out, int max);
int spawner_slots_get(int which);
void spawner_slots_set(int which, int value);
}
namespace {
struct SpawnItem { const char *id, *name; int cat, life, odd; };
const SpawnItem kSpawnItems[] = {
#include "spawner_items.inc"
};
bool ci_contains(const char *hay, const char *needle) {
    if (!*needle) return true;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) h++, n++;
        if (!*n) return true;
    }
    return false;
}
// Long lists: rows far outside the view are only a gap of the same height (drawing all 1,700 every frame costs), with
// a margin of rows above and below so the controller's cursor can still step onto the next one and scroll.
bool row_near_view(void) {
    float h = ImGui::GetFontSize() * 1.9f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    const ImRect &clip = ImGui::GetCurrentWindow()->ClipRect;
    float m = h * 8;
    if (p.y + h < clip.Min.y - m || p.y > clip.Max.y + m) {
        ImGui::Dummy(ImVec2(1, h));
        return false;
    }
    return true;
}
}  // namespace

// Items > Remove: the pouch as the game has it (runtime/spawner.c spawner_inventory), one row per item name.
static void tab_items_remove(const char *query, int cat, int amount) {
    static InvItem inv[600];
    int n = spawner_inventory(inv, 600);
    if (n < 0) {
        opt_section("Your pouch");
        ImGui::TextWrapped("Load your save to see what's in your pouch.");
        return;
    }
    // names: the spawner's own list, else the ID
    static std::unordered_map<std::string, const SpawnItem *> names;
    if (names.empty())
        for (const SpawnItem &it : kSpawnItems) names[it.id] = &it;
    // pouch types: 0 sword, 1 bow, 2 arrow, 3 shield, 4-6 armour, 7 material, 8 food, 9 key item -> the menu's kinds
    auto kind = [](int t) { return t <= 3 ? t : t <= 6 ? 4 : t == 7 ? 5 : t == 8 ? 6 : 7; };
    struct Row { std::string id; const char *name; int kind, slots, unequipped, value; };
    std::vector<Row> rows;
    std::unordered_map<std::string, size_t> at;
    for (int i = 0; i < n; i++) {
        auto f = at.find(inv[i].name);
        if (f == at.end()) {
            auto nm = names.find(inv[i].name);
            at[inv[i].name] = rows.size();
            rows.push_back(Row{inv[i].name, nm != names.end() ? nm->second->name : NULL, kind(inv[i].type), 0, 0, 0});
            f = at.find(inv[i].name);
        }
        Row &r = rows[f->second];
        r.slots++;
        if (!inv[i].equipped) r.unequipped++;
        r.value += inv[i].value;
    }
    static const char *catname[] = {"Weapon", "Bow", "Arrows", "Shield", "Armour", "Material", "Food", "Key item"};
    auto stacks = [](const Row &r) { return r.kind == 2 || r.kind == 5 || r.kind == 6 || (r.kind == 7 && r.slots == 1 && r.value > 1); };
    // how many removeItem calls take all of a row (equipped ones stay)
    auto all_of = [&](const Row &r) { return stacks(r) ? r.value : r.unequipped; };
    std::vector<const Row *> shown;
    for (const Row &r : rows) {
        if (cat >= 9 ? false : cat && r.kind != cat - 1) continue;
        if (!ci_contains(r.name ? r.name : r.id.c_str(), query) && !ci_contains(r.id.c_str(), query)) continue;
        shown.push_back(&r);
    }
    opt_section("Remove several at once");
    {
        int armour = 0;
        for (const Row &r : rows) if (r.kind == 4) armour += r.unequipped;
        char v[32];
        snprintf(v, sizeof v, "%d pieces", armour);
        if (!armour) opt_disable_next();
        if (opt_action("Remove all unequipped armour", v, "Takes every armour piece you aren't wearing out of the pouch. Save first: there's no undo.", false))
            for (const Row &r : rows) if (r.kind == 4 && r.unequipped) spawner_remove_request(r.id.c_str(), r.unequipped);
        int listed = 0;
        for (const Row *r : shown) listed += all_of(*r) > 0;
        snprintf(v, sizeof v, "%d items", listed);
        if (!listed) opt_disable_next();
        if (opt_action("Remove everything listed below", v, "Removes all of every item the list below shows (pick a Category or type in Search to narrow it). Equipped items stay. Save first: there's no undo.", false))
            for (const Row *r : shown) if (all_of(*r) > 0) spawner_remove_request(r->id.c_str(), all_of(*r));
    }
    opt_section("Your pouch: select one to remove it");
    for (size_t i = 0; i < shown.size(); i++) {
        const Row &r = *shown[i];
        if (!row_near_view()) continue;
        char val[64], desc[300];
        if (stacks(r)) snprintf(val, sizeof val, "x%d", r.value);
        else if (r.slots > 1) snprintf(val, sizeof val, "%d%s", r.slots, r.unequipped < r.slots ? " (1 worn)" : "");
        else snprintf(val, sizeof val, "%s", r.unequipped ? catname[r.kind] : "worn / equipped");
        snprintf(desc, sizeof desc, "%s  (%s)  Removes %s. Equipped items stay: unequip them first.", r.id.c_str(),
                 catname[r.kind], stacks(r) ? "the Amount set above from the stack" : "the Amount set above, one piece each");
        ImGui::PushID((int)i);
        bool can = all_of(r) > 0;
        if (!can) opt_disable_next();
        if (opt_action(r.name ? r.name : r.id.c_str(), val, desc, false) && can) {
            int k = amount < all_of(r) ? amount : all_of(r);
            spawner_remove_request(r.id.c_str(), k);
        }
        ImGui::PopID();
    }
    if (shown.empty()) ImGui::TextWrapped(n ? "Nothing in your pouch matches." : "Your pouch is empty.");
}

static void tab_items(void) {
    opt_begin("##items", ImGui::GetFontSize() / 34);
    if (!spawner_available()) {
        opt_section("Items (beta)");
        opt_disable_next();
        opt_action("Add items", "", "The item spawner needs game version 1.9.0.", false);
        opt_end();
        return;
    }
    static char query[64];
    static int cat = 0;
    static float amount = 1;
    static int mode = 0;
    static const char *modes[] = {"Add", "Remove"};
    static const char *cats[] = {"All", "Weapons", "Bows", "Arrows", "Shields", "Armour", "Materials", "Food", "Key items",
                                  "Unobtainable (may crash)", "Added by mods"};
    opt_section("Add items (beta): may break things");
    opt_search("Search", query, sizeof query, "Search: type part of a name",
               "Type part of an item's name, like \"lynel\" or \"apple\", or its ID, like Weapon_Sword_070. The list "
               "below narrows as you type.");
    opt_choice("Mode", &mode, modes, 2, 0,
               "Add: selecting an item puts it in your pouch. Remove: lists what is in your pouch; selecting one takes it out (the "
               "Amount from a stack, or that many slots of gear). Equipped items are kept: unequip them first.");
    opt_choice("Category", &cat, cats, 11, 0, "Shows only one kind of item. Unobtainable: things the game never lets you carry (NPC tools, boss weapons, unused gear). They need the Unobtainable items mod and may crash the game when equipped, dropped or shown. Added by mods: items your mods add to the game (listed by their IDs).");
    opt_slider("Amount", &amount, 1, 999, 1, 1, "%g",
               "How many to add or remove. Things that stack (materials, food, arrows) go into one slot, up to 999. "
               "Weapons, bows, shields and armour take a slot each, up to 20 at a time when adding. Removing more than "
               "you have just empties it.");
    if (const char *st = spawner_status(); st && *st) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.88f, 0.33f, 1));
        ImGui::TextWrapped("%s", st);
        ImGui::PopStyleColor();
    }
    {
        opt_section("Pouch slots (beta)");
        static const char *lab[3] = {"Weapon slots", "Bow slots", "Shield slots"};
        static const float lo[3] = {8, 5, 4}, game_max[3] = {20, 14, 20};
        for (int k = 0; k < 3; k++) {
            int cur = spawner_slots_get(k);
            static float val[3] = {-1, -1, -1};
            static int last_seen[3] = {-2, -2, -2};
            if (cur >= 0 && cur != last_seen[k]) val[k] = (float)cur, last_seen[k] = cur;  // (follow the game's own value)
            if (cur < 0) {
                opt_disable_next();
                opt_action(lab[k], "...", "Load your save to change this.", false);
                continue;
            }
            char d[220];
            snprintf(d, sizeof d, "How many the pouch holds. The game stops at %g (Hestu's seeds); past that needs the Raised slot "
                     "limits mod, up to 100. Extra items go onto more pages of the tab.", game_max[k]);
            if (opt_slider(lab[k], &val[k], lo[k], 100, 1, lo[k], "%g", d)) spawner_slots_set(k, (int)val[k]);
        }
        {
            float a = (float)g_settings.armor_slots;
            if (spawner_armor_state() < 0) opt_disable_next();
            if (opt_slider("Armour slots", &a, 100, 300, 5, 100, "%g",
                           spawner_armor_state() < 0
                               ? "This game build's armour limit couldn't be found, so it stays at 100."
                               : "How many armour pieces the pouch takes before the game says it's full. The game's own: "
                                 "100. Kept between sessions. The pouch holds 420 items of all kinds together, so very "
                                 "full armour leaves less room for the rest.")) {
                g_settings.armor_slots = (int)a;
                settings_save();
            }
        }
    }
    // weapon stats: durability and a bonus (act::WeaponModifier), for weapons, bows and shields added from here
    static float durability = 0, amt[3] = {10, 1.5f, 5};
    static int bonus = 0;
    static bool yellow = true;
    static const char *bonuses[] = {"None", "Attack up", "Durability up", "Critical hit", "Long throw", "Multi-shot",
                                    "Quick shot", "Shield guard up", "Zoom rapid (unused)", "Shield surf (unused)"};
    static const uint32_t bonus_flag[] = {0, 0x1, 0x2, 0x4, 0x8, 0x10, 0x40, 0x100, 0x20, 0x80};
    // what the amount means: 0 a number added, 1 a multiplier (x1000 in the game), 2 a count, 3 none
    static const int bonus_kind[] = {3, 0, 0, 3, 1, 2, 1, 0, 3, 1};
    if (mode == 0) {
        opt_section("Weapon stats (weapons, bows, shields)");
        opt_slider("Durability", &durability, 0, 5000, 5, 0, "%g",
                   "How much use it has left. 0: the item's own (full). The game may still cap it at the weapon's most.");
        opt_choice("Bonus", &bonus, bonuses, 10, 0,
                   "The bonus shown with a + on the item. Attack up and Durability up: swords, spears, bows. Critical "
                   "hit and Long throw: melee weapons. Multi-shot and Quick shot: bows. Shield guard up: shields. "
                   "A bonus the item can't have does nothing (or may confuse the game).");
        int kind = bonus_kind[bonus];
        if (kind == 0) {
            opt_slider("Bonus amount", &amt[0], 1, 9999, 1, 10, "+%g", "Added to the item's attack, durability or "
                       "guard. The game's own go up to about +60 (attack) and +100 (guard).");
        } else if (kind == 1) {
            opt_slider("Bonus amount", &amt[1], 0.1f, 20, 0.1f, 1.5f, "x%.1f", "A multiplier: how much farther it "
                       "flies (Long throw, the game's own 1.5 to 2) or how much faster the bow draws (Quick shot, 1.1 to 1.3).");
        } else if (kind == 2) {
            opt_slider("Bonus amount", &amt[2], 1, 20, 1, 5, "%g arrows", "Arrows per shot. The game's own: 3 or 5.");
        }
        if (bonus) opt_toggle("Yellow bonus", &yellow, true, "Shows the bonus in yellow, as on the strongest gear the game hands out.");
    }
    {
        opt_section("Link (beta)");
        static const char *lab[6] = {"Jump height", "Glide speed", "Swim speed", "Revali's Gale height", "Stamina use",
                                     "Climb dash and ladder speed"};
        static const char *desc[6] = {
            "Multiplies how high Link jumps (also off walls and ladders). 1x is the game's own.",
            "Multiplies the paraglider's top speeds. 1x is the game's own.",
            "Multiplies Link's swimming speeds, including the swim dash. 1x is the game's own.",
            "Multiplies how high Revali's Gale throws Link. 1x is the game's own.",
            "Multiplies the stamina that sprinting, jumping, gliding, swimming and climbing take. 0x: none at all.",
            "Multiplies the climbing dash (jump while climbing) and ladder speeds. 1x is the game's own.",
        };
        static const int order[6] = {0, 1, 2, 5, 3, 4};
        for (int j = 0; j < 6; j++) {
            int k = order[j];
            float v = spawner_tweak_get(k);
            if (opt_slider(lab[k], &v, 0, k == 3 ? 5 : 10, 0.1f, 1, "%.1fx", desc[k])) spawner_tweak_set(k, v);
        }
        int found = spawner_tweak_found();
        if (found >= 0 && found < spawner_tweak_total()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.4f, 1));
            ImGui::TextWrapped("Found %d of Link's %d numbers: some of these sliders may do nothing (savagewind.log lists "
                               "which).", found, spawner_tweak_total());
            ImGui::PopStyleColor();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1));
        ImGui::TextWrapped("Walking and running speed come from Link's animations, not a number the game reads, so there "
                           "is no slider for them yet.");
        ImGui::PopStyleColor();
    }
    static const char *catname[] = {"Weapon", "Bow", "Arrows", "Shield", "Armour", "Material", "Food", "Key item"};
    if (mode == 1) {  // Remove: what's in the pouch now
        tab_items_remove(query, cat, (int)amount);
        opt_end();
        return;
    }
    opt_section("Items: select one to add it");
    int total = 0;
    // the list: the spawner's own, then what the game's actor table has beyond it (mods' new items: odd 2)
    static std::vector<SpawnItem> all;
    if (all.empty()) {
        all.assign(kSpawnItems, kSpawnItems + sizeof kSpawnItems / sizeof kSpawnItems[0]);
        std::unordered_set<std::string> known;
        for (const SpawnItem &it : all) known.insert(it.id);
        const ModItem *mi = NULL;
        int nm = spawner_mod_items(&mi);
        for (int k = 0; k < nm; k++)
            if (!known.count(mi[k].id)) all.push_back(SpawnItem{mi[k].id, mi[k].id, mi[k].cat, mi[k].life, 2});
    }
    for (int i = 0; i < (int)all.size(); i++) {
        const SpawnItem &it = all[i];
        if (cat == 10 ? it.odd != 2 : cat == 9 ? it.odd != 1 : cat && (it.cat != cat - 1 || it.odd == 1)) continue;
        if (!ci_contains(it.name, query) && !ci_contains(it.id, query)) continue;
        total++;
        if (!row_near_view()) continue;
        bool gear = it.cat == 0 || it.cat == 1 || it.cat == 3;
        bool stack = it.cat == 2 || it.cat == 5 || it.cat == 6;  // (the game stacks these; key items it decides itself)
        char desc[300];
        if (it.odd == 2) {
            snprintf(desc, sizeof desc, "%s  (%s)  Added by a mod: shown by its ID. If the mod's files aren't all "
                     "there, adding it may crash the game - save first.", it.id, catname[it.cat]);
        } else if (it.odd) {
            snprintf(desc, sizeof desc, "%s  (%s)  Unobtainable: needs the Unobtainable items mod. May crash the game when "
                     "equipped, dropped or shown in the inventory - save first.", it.id, catname[it.cat]);
        } else
        snprintf(desc, sizeof desc, "%s  (%s)%s", it.id, catname[it.cat],
                 gear ? "  Full durability." : it.cat == 6 && !strncmp(it.id, "Item_Cook", 9) ? "  Cooked dishes come without their effects." : "");
        ImGui::PushID(i);
        char valtxt[48];
        snprintf(valtxt, sizeof valtxt, it.odd == 2 ? "%s (mod)" : it.odd ? "%s (unobtainable)" : "%s", catname[it.cat]);
        if (mode == 1 && opt_action(it.name, valtxt, desc, false)) {
            spawner_remove_request(it.id, (int)amount);
        } else if (mode == 0 && opt_action(it.name, valtxt, desc, false)) {
            int n = (int)amount;
            if (!stack && n > 20) n = 20;
            int life = durability >= 1 ? (int)durability : it.life > 0 ? it.life : 10;
            int value = gear ? life * 100 : it.cat == 4 ? 0 : n;
            uint32_t mf = gear && bonus ? bonus_flag[bonus] | (yellow ? 0x80000000u : 0) : 0;
            int kind = bonus_kind[bonus];
            int32_t mv = kind == 0 ? (int32_t)amt[0] : kind == 1 ? (int32_t)lroundf(amt[1] * 1000) : kind == 2 ? (int32_t)amt[2] : 0;
            spawner_request_mod(it.id, value, it.cat == 7 ? 1 : n, stack || it.cat == 7, mf, mv);
        }
        ImGui::PopID();
    }
    if (!total) ImGui::TextWrapped("No items match.");
    opt_end();
}

// Spawn: creates enemies, bosses, animals, NPCs and objects in front of Link (runtime/spawner.c spawner_actor_request).
extern "C" int spawner_actor_request(const char *name, int count, float dist);
namespace {
struct SpawnActor { const char *id, *name, *profile; int group; };
const SpawnActor kSpawnActors[] = {
#include "spawner_actors.inc"
};
}  // namespace

static void tab_spawn(void) {
    opt_begin("##spawn", ImGui::GetFontSize() / 34);
    if (!spawner_available()) {
        opt_section("Spawn (beta)");
        opt_disable_next();
        opt_action("Spawn", "", "The spawner needs game version 1.9.0.", false);
        opt_end();
        return;
    }
    static char query[64];
    static int group = 0;
    static float count = 1, dist = 6;
    static const char *groups[] = {"All", "Enemies", "Big enemies", "Bosses", "Animals", "Horses", "NPCs", "Objects",
                                   "Cutscene actors"};
    static const char *gname[] = {"Enemy", "Big enemy", "Boss", "Animal", "Horse", "NPC", "Object", "Cutscene"};
    opt_section("Spawn (beta): may break things");
    opt_search("Search", query, sizeof query, "Search: type part of a name",
               "Type part of a name, like \"lynel\", \"hinox\" or \"beedle\", or an actor name, like Enemy_Lynel_Senior. "
               "The list below narrows as you type.");
    opt_choice("Group", &group, groups, 9, 0,
               "Shows one kind. Bosses, cutscene actors and many objects expect their own arena, event or setup: they may "
               "not move, may vanish, or may crash the game. Save first.");
    opt_slider("How many", &count, 1, 10, 1, 1, "%g", "How many to create, in a row across Link's view.");
    opt_slider("Distance", &dist, 2, 40, 1, 6, "%g m", "How far in front of Link they appear. Big enemies and bosses need room.");
    if (const char *st = spawner_status(); st && *st) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.88f, 0.33f, 1));
        ImGui::TextWrapped("%s", st);
        ImGui::PopStyleColor();
    }
    opt_section("Select one to spawn it");
    int total = 0;
    for (int i = 0; i < (int)(sizeof kSpawnActors / sizeof kSpawnActors[0]); i++) {
        const SpawnActor &a = kSpawnActors[i];
        if (group && a.group != group - 1) continue;
        if (!ci_contains(a.name, query) && !ci_contains(a.id, query)) continue;
        total++;
        if (!row_near_view()) continue;
        char desc[260];
        snprintf(desc, sizeof desc, "%s  (%s)%s", a.id, a.profile,
                 a.group == 2 || a.group == 7 ? "  Expects its own arena or cutscene: may not act normally, or may crash." : "");
        ImGui::PushID(i);
        if (opt_action(a.name, gname[a.group], desc, false)) spawner_actor_request(a.id, (int)count, dist);
        ImGui::PopID();
    }
    if (!total) ImGui::TextWrapped("Nothing matches.");
    opt_end();
}

static void tab_teleport(void) {
    opt_begin("##teleport", ImGui::GetFontSize() / 34);
    static const char *const go = "The game restarts: press Continue and you land there, from your last save. Unsaved progress is lost.";
    opt_section("Bookmarks");
    for (int i = 0; i < TELEPORT_BOOKMARKS; i++) {
        char l[32];
        snprintf(l, sizeof l, "Go to bookmark %d", i + 1);
        bool set = teleport_bookmark_set(i);
        if (!set) opt_disable_next();
        if (opt_action(l, set ? teleport_bookmark_text(i) : tr("Empty"), tr(go), false)) teleport_go_bookmark(i);
        snprintf(l, sizeof l, "Save here as bookmark %d", i + 1);
        if (opt_action(l, "", tr("Remembers where Link is standing. Outdoors only: inside a shrine it leads somewhere else."), false)) teleport_save_here(i);
    }
    opt_section("Places");
    for (int i = 0; i < teleport_place_count(); i++)
        if (opt_action(teleport_place_name(i), "", tr(go), false)) teleport_go_place(i);
    opt_end();
}

static void tab_audio(void) {
    opt_begin("##audio", ImGui::GetFontSize() / 34);
    opt_section("Sound");
    float pct = roundf(g_settings.volume * 100);
    if (opt_slider("Volume", &pct, 0, 200, 5, 100, "%g%%",
                   "The game's volume. 100% is the game's own level; above it, louder."))
        g_settings.volume = pct / 100.0f;
    opt_toggle("Room reverb", &g_settings.reverb, true,
               "The game's echo in caves, dungeons and big rooms.");
    opt_toggle("Movie audio", &g_settings.movie_audio, true,
               "Sound in the game's movies (the opening and the ending). Applies the next time a movie starts.");
    opt_end();
}

extern "C" void rt_request_reboot(int kind);  // runtime/main.c (RT_REBOOT_GAME = 1, RT_REBOOT_TITLE = 2)

// Text and voice languages (runtime/language.c). The game reads both when it starts: in game they apply after
// Quit > Start screen and Play.
static void tab_language(bool in_game) {
    opt_begin("##language", ImGui::GetFontSize() / 34);
    opt_section("Language");
    static const char *tnames[32], *vnames[32];
    for (int i = 0; i < g_ntext_languages && i < 32; i++) tnames[i] = g_text_languages[i].name;
    for (int i = 0; i < g_nvoice_languages && i < 32; i++) vnames[i] = g_voice_languages[i].name;
    const char *later = in_game ? tr("\n\nApplies the next time the game starts: Restart now below, or Quit > Start "
                                     "screen, then Play.") : "";
    char d1[600], d2[800];
    snprintf(d1, sizeof d1, tr("The language of the game's texts and of these menus. Auto: your system's language "
                               "(English if the game doesn't have it).%s"), later);
    snprintf(d2, sizeof d2, tr("The language the characters speak. Same as text: the voices the game picks for the text "
                               "language (Japanese for Korean and Chinese, English for the other languages it has no "
                               "voices for). Any other: those voices with any text language.%s"), later);
    int text = language_text_index();  // (of the languages the game files have: Thai from 1.9.0 on)
    if (opt_choice("Text", &text, tnames, language_text_count(), 0, d1, false)) g_settings.text_language = text;
    opt_choice("Voices", &g_settings.voice_language, vnames, g_nvoice_languages, 0, d2, false);
    if (in_game && language_pending() &&
        opt_button("Restart now", "Starts the game again with these languages. Progress since your last save is lost.")) {
        settings_save();
        rt_request_reboot(1);  // (RT_REBOOT_GAME: boot again at once)
    }
    opt_end();
}

// Quit: back to the start screen (in game: the game reboots to it) or out to the desktop. Each asks first (progress
// since the last save is lost); B / Esc or Cancel backs out.
extern "C" void rt_request_reboot(int kind);  // runtime/main.c (RT_REBOOT_TITLE = 2: the start screen)
static void tab_quit(float scale, bool in_game) {
    static int ask;  // 0: the two choices; 1: start screen?; 2: desktop?
    if (ask && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        ask = 0;
    const char *lost = in_game ? tr("\n\nProgress since your last save is lost.") : "";
    char d1[400], d2[400];
    snprintf(d1, sizeof d1, tr("Leaves the game and goes back to the Savage Wind start screen.%s"), lost);
    snprintf(d2, sizeof d2, tr("Closes Savage Wind.%s"), lost);
    opt_begin("##quit", scale);
    opt_section("Quit");
    if (!ask) {
        if (!in_game) opt_disable_next();  // (already on it)
        if (opt_action("Quit to Start Screen", "", in_game ? d1 : "You're on the start screen.")) ask = 1;
        if (opt_action("Quit to Desktop", "", d2)) ask = 2;
    } else {
        if (opt_action(ask == 1 ? "Yes, quit to Start Screen" : "Yes, quit to Desktop", "", ask == 1 ? d1 : d2)) {
            settings_save();
            if (ask == 1) {
                rt_request_reboot(2);
                set_open(false);
            } else {
                SDL_Event q = {};
                q.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&q);
            }
            ask = 0;
        }
        if (opt_action("Cancel", "", "Stays where you are.")) ask = 0;
    }
    opt_end();
}

static void tab_about(void) {
    header("Movement");
    toggle("D-pad as left stick", &g_settings.dpad_move);
    hint("The d-pad (and the arrow keys) also move like the left stick while the stick is at rest.");
    ImGui::Spacing();
    header("Controller camera");
    opt_toggle("Controller camera speed (beta)", &g_settings.stick_cam_on, false,
               "Turns the camera faster with the right stick than the game allows, with the two sliders below. Off: the "
               "game's own speed. Beta, may break things.");
    if (g_settings.stick_cam_on) {
        float cx = (float)g_settings.stick_cam_x, cy = (float)g_settings.stick_cam_y;
        if (opt_slider("Left and right speed (beta)", &cx, 100, 600, 25, 200, "%g%%", "100% is the game's own speed. Beta, may break things."))
            g_settings.stick_cam_x = (int)cx;
        if (opt_slider("Up and down speed (beta)", &cy, 100, 600, 25, 200, "%g%%", "100% is the game's own speed. Beta, may break things."))
            g_settings.stick_cam_y = (int)cy;
    }
    ImGui::Spacing();
    header("Mouse");
    opt_toggle("PC mouse pointer (beta)", &g_settings.pc_cursor, true,
               "Point and click in menus and map. Esc pauses/goes back; F2 frees the cursor.");
    opt_toggle("Mouse bow aiming (beta)", &g_settings.pc_bow, true,
               "Hold right mouse to aim; left click fires. Off: use your normal button bindings.");
    toggle("Mouse camera", &g_settings.mouse_camera);
    hint("The mouse turns the camera. Pointer off: Esc frees it; pointer on: F2 frees it.");
    float sens = roundf(g_settings.mouse_sens * 100);
    if (opt_slider("Mouse sensitivity", &sens, 10, 1000, 5, MOUSE_SENS_DEFAULT * 100, "%g%%", "How fast the mouse turns the camera."))
        g_settings.mouse_sens = sens / 100.0f;
    float sens_y = roundf(g_settings.mouse_sens_y * 100);
    if (opt_slider("Mouse vertical sensitivity", &sens_y, 50, 200, 5, 100, "%g%%",
                   "Up and down, as a percentage of the mouse sensitivity."))
        g_settings.mouse_sens_y = sens_y / 100.0f;
    toggle("Invert mouse Y", &g_settings.mouse_invert_y);
    ImGui::Spacing();
    header("Controls");
    controls_bindings_draw(ImGui::GetFontSize() / 34);
    ImGui::Spacing();
    header("Menu");
    ImGui::TextUnformatted(tr("Open / close this menu:  the controller's Home / Xbox / PlayStation button, or Back + Start"));
    ImGui::TextUnformatted(tr("Switch tabs:  L / R  (Q / E)        Back:  B  (Esc)"));
    ImGui::TextUnformatted(tr("Fullscreen:  F11 / Alt+Enter"));
}

// Reboot (runtime/main.c): the game is gone - menu and name dialog close; the ImGui context stays.
RT_ON_RESET(menu_reset) {
    set_open(false);
    g_kbd = 0;
    osk_cancel();
    pthread_mutex_lock(&q_lock);
    q_events.clear();
    pthread_mutex_unlock(&q_lock);
}

// Performance overlay (F10), in the style of MangoHud: the game's frame rate and frame times, and the machine's load
// and temperatures (runtime/perfmon.c). Numbers the platform doesn't give read "-".
static void perf_cell(const char *fmt, float v) {
    ImGui::TableNextColumn();
    if (v < 0) ImGui::TextDisabled("-");
    else ImGui::Text(fmt, v);
}
#ifndef __APPLE__
extern "C" bool vk_last_hitch(float *ms, const char **cause, float *ago_s);  // (vulkan/exec.c)
#endif
static void perf_overlay_draw(ImGuiIO &io, float scale) {
    PerfStats s;
    perfmon_snapshot(&s);
    // top right: BOTW keeps its hearts and stamina top left, its map and gauges bottom right
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16 * scale, 16 * scale), 0, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##perf", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(0.72f);
    const ImVec4 gpu_c(0.55f, 0.83f, 0.31f, 1), cpu_c(0.18f, 0.59f, 0.80f, 1), ram_c(0.80f, 0.45f, 0.70f, 1),
                 fps_c(0.95f, 0.95f, 0.95f, 1);
    if (g_settings.perf_overlay == PERF_FPS) {
        ImGui::TextColored(fps_c, "%.0f FPS  %.1f ms", s.fps, s.frame_ms_avg);
        ImGui::End();
        return;
    }
    if (ImGui::BeginTable("##perft", 4, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextColumn(); ImGui::TextColored(gpu_c, "GPU");
        perf_cell("%.0f%%", s.gpu_pct); perf_cell("%.0f C", s.gpu_temp_c); perf_cell("%.0f W", s.gpu_power_w);
        ImGui::TableNextColumn(); ImGui::TextColored(gpu_c, "VRAM");
        perf_cell("%.2f GB", s.vram_used_mb < 0 ? -1 : s.vram_used_mb / 1024); perf_cell("/ %.1f GB", s.vram_total_mb < 0 ? -1 : s.vram_total_mb / 1024);
        perf_cell("%.0f MHz", s.gpu_clock_mhz);
        ImGui::TableNextColumn(); ImGui::TextColored(cpu_c, "CPU");
        perf_cell("%.0f%%", s.cpu_total_pct); perf_cell("%.0f C", s.cpu_temp_c); perf_cell(tr("game %.0f%%"), s.cpu_game_pct);
        ImGui::TableNextColumn(); ImGui::TextColored(ram_c, "RAM");
        perf_cell("%.1f GB", s.ram_used_mb / 1024); perf_cell("/ %.1f GB", s.ram_total_mb / 1024); perf_cell(tr("game %.1f GB"), s.ram_game_mb / 1024);
        ImGui::TableNextColumn(); ImGui::TextColored(fps_c, "FPS");
        perf_cell("%.0f", s.fps);
        perf_cell("%.1f ms", s.frame_ms_avg); perf_cell(tr("max %.1f ms"), s.frame_ms_max);
        ImGui::EndTable();
        {   // the last 4 seconds or so of frame times, up to 33 ms
            static float hist[240];
            static int at;
            hist[at++ % 240] = io.DeltaTime * 1000;
            ImGui::PlotLines("##frames", hist, 240, at % 240, NULL, 0, 33.3f, ImVec2(ImGui::GetContentRegionAvail().x, 48 * scale));
        }
    }
    float top = s.frame_ms_max > 50 ? s.frame_ms_max : 50;
    ImGui::PlotLines("##ft", s.frame_ms, PERF_HISTORY, 0, NULL, 0, top, ImVec2(330 * scale, 48 * scale));
    ImGui::TextDisabled("%s", s.gpu_name[0] ? s.gpu_name : tr("GPU: no NVML / sysfs data"));
    ImGui::TextDisabled("%s", s.cpu_name);
    // the graphics settings in effect: what the game falls back to (a mode that isn't offered), not
    // what the menu says
    int up = upscaler_available(g_settings.upscaler) ? g_settings.upscaler : UPSCALE_OFF;
    std::string line = std::string(tr("Upscaler")) + ": ";
    switch (up) {
        case UPSCALE_METALFX: line += tr("MetalFX"); break;
        case UPSCALE_FSR1: line += tr("FSR 1"); break;
        default: line += tr("Off");
    }
    int q = g_settings.upscale_quality;
    if (up != UPSCALE_OFF && upscale_factor() > 1.0) {
        char f[16];
        snprintf(f, sizeof f, "%.1f", upscale_factor());
        line += std::string(" ") + (q >= 0 && q < UPQ_COUNT ? std::string(tr(k_upscale_modes[q])) + " " : "") + f + "x";
    }
    int aa = rt_antialias();
    line += std::string(" | ") + tr("Anti-aliasing") + ": " + (rt_fsr4() ? "AMD FSR 4" : aa == AA_TAA ? tr("TAA") : aa == AA_SMAA ? tr("SMAA") : tr("Off"));
    ImGui::TextDisabled("%s", line.c_str());
    line.clear();
#ifndef __APPLE__
    {   // the scene's size and the final image's (the HUD and menus render at that one)
        float rx, ry, ox, oy;
        renderer_get_scene_scale(&rx, &ry);
        renderer_get_scale(&ox, &oy);
        if (rx > 0 && ox > 0) {
            char r[48];
            snprintf(r, sizeof r, "%.0f x %.0f -> %.0f x %.0f", 1600.0 * rx, 900.0 * ry, 1920.0 * ox, 1080.0 * oy);
            line = r;
        }
    }
#endif
    if (!line.empty()) ImGui::TextDisabled("%s", line.c_str());
    // Link's coordinates; no line at all while there is no Link or the game is in a scene (title, loading, movie, event).
    // Axis letters are not translated.
    float pos[3];
    if (perfmon_position(pos)) ImGui::Text("X %.1f  Y %.1f  Z %.1f", pos[0], pos[1], pos[2]);
#ifndef __APPLE__
    {   // what the last long frame was (vulkan/exec.c), for a minute
        float ms, ago;
        const char *cause;
        if (vk_last_hitch(&ms, &cause, &ago) && ago < 60)
            ImGui::Text(tr("Last hitch: %.0f ms, %s, %.0f s ago"), ms, tr(cause), ago);
    }
#endif
    ImGui::End();
}

static void keyboard_draw(ImGuiIO &io, float scale) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 120));
    float w = fminf(io.DisplaySize.x * 0.9f, 1100 * scale);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Always);
    ImGui::Begin("##kbd", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushFont(g_font_big);
    ImGui::TextColored(ImVec4(0.93f, 0.76f, 0.33f, 1), "%s", tr("Enter your name"));
    ImGui::PopFont();
    static bool was_open, was_pad;
    bool pad = g_last_pad.load(), appearing = !was_open || pad != was_pad;  // (switching device: focus the new one)
    was_open = true, was_pad = pad;
    if (!pad) {  // keyboard: type straight into the field
        if (appearing) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        ImGui::PushFont(g_font_big);
        ImGui::InputText("##name", g_kbd_text, (size_t)(g_kbd_max * 4 + 1 < (int)sizeof g_kbd_text ? g_kbd_max * 4 + 1 : sizeof g_kbd_text),
                         ImGuiInputTextFlags_CallbackCharFilter,
                         [](ImGuiInputTextCallbackData *d) -> int { return d->EventChar < 0x20 ? 1 : 0; });
        ImGui::PopFont();
        if ((int)strlen(g_kbd_text) > g_kbd_max) g_kbd_text[g_kbd_max] = 0;
        ImGui::TextDisabled(tr("Enter  confirm      Esc  cancel      (%d / %d)"), (int)strlen(g_kbd_text), g_kbd_max);
    } else {  // controller: on-screen keyboard (osk.mm, shared with our menus' text fields)
        int r = osk_keys(g_kbd_text, sizeof g_kbd_text, g_kbd_max, appearing, scale, g_font_big);
        if (r) g_kbd = r > 0 ? 2 : 3;  // (Start / + confirms in menu_handle_event too)
    }
    ImGui::End();
    if (g_kbd.load() != 1) was_open = false;
}

// Window size's list, dropped down under the row's button on the game's Options screen (Video), drawn like the game's
// own pop-up lists: the menu is 16:9 in the middle of the window (output_scale.c), the button where its layout has it
static void size_list_draw(ImGuiIO &io, float scale) {
    static bool was_open;
    static int frames;
    bool appearing = !was_open;
    was_open = true;
    frames = appearing ? 0 : frames + 1;
    float gw = io.DisplaySize.x, gh = gw * 9 / 16;
    if (gh > io.DisplaySize.y) gh = io.DisplaySize.y, gw = gh * 16 / 9;
    float ox = (io.DisplaySize.x - gw) * 0.5f, oy = (io.DisplaySize.y - gh) * 0.5f;
    float x0 = ox + gw * 0.496f, x1 = ox + gw * 0.708f, y0 = oy + gh * 0.318f, bottom = oy + gh * 0.87f;
    float row_h = gh * 0.046f;
    WinSize list[WINSIZE_MAX];
    static char names[WINSIZE_MAX][64];
    int n = winsize_list(list, g_settings.fullscreen), cur = 0;
    WinSize e = winsize_effective(g_settings.fullscreen);
    for (int i = 0; i < n; i++) {
        winsize_name(list[i], tr("native"), names[i], sizeof names[i]);
        if (list[i].w == e.w && list[i].h == e.h) cur = i;
    }
    ImGui::SetNextWindowPos(ImVec2(x0, y0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(x1 - x0, 0), ImVec2(x1 - x0, bottom - y0));
    if (appearing) ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4 * scale, 4 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.04f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.62f, 0.62f, 0.58f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1, 1, 1, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0.95f, 0.93f, 0.85f, 1));
    ImGui::Begin("##sizes", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PushFont(g_menu_font);
    int picked = -1;
    for (int i = 0; i < n; i++) {
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##size", i == cur, 0, ImVec2(w, row_h))) picked = i;
        if (appearing && i == cur) {
            ImGui::SetItemDefaultFocus();
            ImGui::SetScrollHereY(0.5f);
            ImGui::SetNavCursorVisible(g_last_pad.load());
        }
        bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ImVec2 ts = ImGui::CalcTextSize(names[i]);
        ImU32 col = i == cur ? IM_COL32(236, 194, 84, 255) : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 213, 205, 255);
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (row_h - ts.y) * 0.5f), col, names[i]);
        ImGui::PopID();
    }
    // (test) SAVAGEWIND_SIZE_PICK=<n>: picks size n three seconds after the list is up
    if (const char *t = getenv("SAVAGEWIND_SIZE_PICK"); t && frames == 180) picked = atoi(t) < n ? atoi(t) : cur;
    bool cancel = !appearing && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                                 ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                 (ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered()));
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
    if (picked >= 0 && picked != cur) {
        g_settings.window_w = list[picked].w, g_settings.window_h = list[picked].h;
        settings_save();
        g_want_window_apply.store(true);
        game_options_size_picked();
    }
    if (picked >= 0 || cancel) {
        g_sizes_hold = SDL_GetTicks() + 300;  // (the A / B that closed it, still down, stays out of the game)
        g_sizes = false;
        was_open = false;
    }
}

// Game thread (runtime/hle/swkbd.c): show the dialog, re-present the last frame until it's answered.
// Returns 1 confirmed (out = text, UTF-8), 0 cancelled.
extern "C" int menu_keyboard_run(const char *initial, int max_len, char *out, int cap) {
    extern void backend_represent(void);
    snprintf(g_kbd_text, sizeof g_kbd_text, "%s", initial ? initial : "");
    g_kbd_max = max_len > 0 && max_len < (int)sizeof g_kbd_text / 4 ? max_len : 8;
    input_reset();
    g_kbd = 1;
    // (test) SAVAGEWIND_KBD_AUTO=<text>: answer at once
    if (getenv("SAVAGEWIND_KBD_AUTO")) { snprintf(g_kbd_text, sizeof g_kbd_text, "%s", getenv("SAVAGEWIND_KBD_AUTO")); g_kbd = 2; }
    while (g_kbd.load() == 1 && !g_rt_unwind) {  // ~60 Hz (a reboot cancels)
        uint64_t t0 = SDL_GetTicks();
        backend_represent();
        uint64_t dt = SDL_GetTicks() - t0;
        if (dt < 16) SDL_Delay((uint32_t)(16 - dt));
    }
    bool ok = g_kbd.load() == 2 && g_kbd_text[0];
    if (ok) snprintf(out, cap, "%s", g_kbd_text);
    g_kbd = 0;
    input_reset();
    return ok;
}

// The tab bar and its pages, into the current window: the in-game menu, and the start screen's Settings (in_game
// false: nothing of the game is loaded yet - the tabs that need it are greyed out; prelaunch: the start screen's own
// first tab, game files and fonts, drawn by launcher.mm).
static void tabs_body(float scale, bool in_game, void (*prelaunch)(float)) {
    // (id: the page; -1 = Prelaunch)
    struct Tab { const char *name; int id; bool on; };
    Tab tabs[13];
    int ntabs = 0;
    if (prelaunch) tabs[ntabs++] = {"Prelaunch", -1, true};
    static const char *names[] = {"Video", "Audio", "Language", "Draw Distance", "Cheats", "Saves", "Achievements", "Teleport", "Controls", "Quit", "Items", "Spawn"};
    static const int order[] = {8, 0, 3, 1, 2, 5, 6, 4, 10, 11, 7, 9};  // Controls, Video, Draw Distance, Audio, Language, Saves, Achievements, Cheats, Teleport, Quit
    for (int i : order) {  // (Quit: in game only - the start screen has its own QUIT; Saves: on the start screen only
        if (i == 9 && prelaunch) continue;  //  the game must not be running while its saves change)
        if (i == 5 && !prelaunch) continue;
        if (i == 7 && prelaunch) continue;  // (Teleport: in game only)
        if ((i == 10 || i == 11) && prelaunch) continue;  // (Items, Spawn: in game only)
        tabs[ntabs++] = {names[i], i, true};
    }
    int cur = 0;  // g_tab / g_tab_request hold ids
    for (int i = 0; i < ntabs; i++) if (tabs[i].id == g_tab) cur = i;
    if (!tabs[cur].on) cur = 0;
    // L/R (or Q/E) switch tabs, skipping greyed-out ones (not while typing: Q / E are letters then, and the on-screen
    // keyboard has the controller)
    if (g_tab_request_ext >= 0) { g_tab_request = g_tab_request_ext; g_tab_request_ext = -1; }
    bool any_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), typing = ui_text_busy();
    static int focused_tab = -99;  // the tab the controller's cursor is on (from the frame before; -99: none)
    if (!any_popup && !typing) {
        int step = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false) ? 1
                 : ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false) ? -1 : 0;
        if (step) {
            int i = cur;
            do i = (i + step + ntabs) % ntabs; while (!tabs[i].on);
            g_tab_request = tabs[i].id;
            // the cursor on the tab bar goes along (left behind, A on it switched straight back; user 2026-10-11)
            if (focused_tab != -99) g_focus_tab = tabs[i].id, g_focus_frames = 0;
        }
    }
    // Down with the cursor on the tab bar: the page's first row (option rows; other pages keep ImGui's own pick)
    bool down_from_tab = focused_tab != -99 && ImGui::GetCurrentContext()->NavCursorVisible && !any_popup &&
                         (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown) ||
                          ImGui::IsKeyPressed(ImGuiKey_DownArrow));
    opt_focus_first(down_from_tab);
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int k = 0; k < ntabs; k++) {
            const Tab &t = tabs[k];
            ImGuiTabItemFlags f = g_tab_request == t.id && t.on ? ImGuiTabItemFlags_SetSelected : 0;
            char lbl[160];
            snprintf(lbl, sizeof lbl, prelaunch ? " %s ###%s" : "  %s  ###%s", tr(t.name), t.name);  // (### : ID stays English)
            ImGui::BeginDisabled(!t.on);
            // focus (controller / keyboard): the tab lights up a little with a gold bar along its bottom, instead of
            // ImGui's outline (it sits 4 px outside the tab and the tab bar's clipping could cut its top off on macOS)
            // (focused_tab: from the frame before - colours go in before the tab is drawn)
            bool lit = focused_tab == t.id && ImGui::GetCurrentContext()->NavCursorVisible;
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
            if (lit) {
                ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.15f, 0.25f, 0.14f, 1));
                ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.15f, 0.25f, 0.14f, 1));
            }
            if (g_focus_tab == -1 && !prelaunch) g_focus_tab = 8;  // (no Prelaunch in game: Controls, the first tab)
            bool focus_here = g_focus_tab == t.id || (g_focus_tab == -100 && t.id == g_tab);
            bool open = ImGui::BeginTabItem(lbl, NULL, f);
            ImGui::PopStyleColor(lit ? 3 : 1);
            if (focus_here) {  // the controller's cursor starts on this tab (SetKeyboardFocusHere skips tabs: no tab stop)
                ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                ImGui::SetNavCursorVisible(true);
                // a few frames: the page's child window appearing makes ImGui pick its first widget once it's laid out
                if (++g_focus_frames >= 4) g_focus_tab = -99, g_focus_frames = 0;
                else g_focus_tab = t.id;
            }
            if (lit) {
                ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                float h = 4 * scale > 2 ? 4 * scale : 2;
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + 2, b.y - h), ImVec2(b.x - 2, b.y),
                                                          IM_COL32(190, 226, 84, 255));  // (the menu accent)
            }
            if (ImGui::IsItemFocused()) focused_tab = t.id;
            else if (focused_tab == t.id) focused_tab = -99;
            ImGui::EndDisabled();
            if (open) {
                g_tab = t.id;
                ImGui::Spacing();
                ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_None,  // (the rest of the window: no hint line)
                                  ImGuiWindowFlags_NavFlattened);
                // Down from the tab bar, pages that aren't option rows (Prelaunch, Controls): their first control
                // (option-row pages: opt_focus_first)
                if (down_from_tab && (t.id == -1 || t.id == 8)) {
                    opt_focus_first(false);
                    ImGui::SetKeyboardFocusHere();
                    ImGui::SetNavCursorVisible(true);
                }
                switch (t.id) {
                case -1: prelaunch(scale); break;
                case 0: tab_display(); break;
                case 1: tab_audio(); break;
                case 3: tab_distance(); break;
                case 4: tab_cheats(); break;
                case 2: tab_language(in_game); break;
                case 5: saves_tab_draw(); break;
                case 6: ach_ui_tab(); break;
                case 7: tab_teleport(); break;
                case 10: tab_items(); break;
                case 11: tab_spawn(); break;
                case 8: tab_about(); break;
                case 9: tab_quit(scale, in_game); break;
                }
                static const char *scroll_test = getenv("SAVAGEWIND_MENU_SCROLL");  // test hook: page scroll, 0..1
                if (scroll_test) ImGui::SetScrollY(ImGui::GetScrollMaxY() * (float)atof(scroll_test));
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        g_tab_request = -2;
        opt_focus_first(false);  // (a page without option rows: nothing took it)
        ImGui::EndTabBar();
    }
}

void menu_tabs_draw(float scale, ImFont *font, ImFont *font_big, bool in_game, void (*prelaunch)(float)) {
    ImFont *f0 = g_font, *f1 = g_font_big;
    g_font = font, g_font_big = font_big;
    ImGuiStyle saved = ImGui::GetStyle();  // the menu's look (the start screen has its own style)
    style_init();
    ImGui::GetStyle().ScaleAllSizes(scale);
    tabs_body(scale, in_game, prelaunch);
    ImGui::GetStyle() = saved;
    g_font = f0, g_font_big = f1;
}

bool menu_render_needed(void) {
    return g_open.load() || g_kbd.load() == 1 || g_sizes.load() || g_settings.perf_overlay != PERF_OFF;
}

#ifdef __APPLE__
void menu_render(id<MTLDevice> dev, id<MTLCommandBuffer> cb, id<MTLTexture> target) {
    const double tw = target.width, th = target.height;
#else
void menu_render(void *cmd, uint32_t target_w, uint32_t target_h) {
    const double tw = target_w, th = target_h;
#endif
    bool open = g_open.load();
    bool kbd = g_kbd.load() == 1;
    bool sizes = g_sizes.load();
    uint64_t tt = g_title_tick.load();
    bool title = tt && SDL_GetTicks() - tt < 300;
    static float title_a;
    if (!title && title_a > 0.004f) title = true;  // (fading out)
    float pointer_box[4];
    bool pointer_hover = pc_cursor_hover_box(pointer_box);
    if (!open && !kbd && !sizes && !title && !pointer_hover && g_settings.perf_overlay == PERF_OFF && !ach_ui_active()) {
        if (g_inited) { pthread_mutex_lock(&q_lock); q_events.clear(); pthread_mutex_unlock(&q_lock); }
        return;
    }
#ifdef __APPLE__
    if (!g_inited) init(dev);
#else
    rec_drain();  // (ImGui records into cmd itself: the title's brand lines too, which menu_render_needed doesn't count)
    if (!g_inited) init();
#endif
    ImGui::SetCurrentContext(g_ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)tw, (float)th);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    static double last;
    double now = SDL_GetTicks() / 1000.0;
    io.DeltaTime = last > 0 && now > last ? (float)(now - last) : 1.0f / 60;
    last = now;
    drain_events(io);
    // fonts are built at 34/56 px for a ~1080 px tall output; scale to the window
    float scale = (float)(th / 1080.0);
    if (scale < 0.45f) scale = 0.45f;
    io.FontGlobalScale = scale;

#ifdef __APPLE__
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
#endif
    // spacing/rounding follow the output size too (style tuned for 1080 p)
    static ImGuiStyle base;
    static float style_scale = -1;
    if (style_scale < 0) base = ImGui::GetStyle();
    if (fabsf(style_scale - scale) > 0.01f) {
        ImGui::GetStyle() = base;
        ImGui::GetStyle().ScaleAllSizes(scale);
        style_scale = scale;
    }
    fonts_follow_language();
#ifdef __APPLE__
    ImGui_ImplMetal_NewFrame(rp);
#else
    ImGui_ImplVulkan_NewFrame();
#endif
    ImGui::NewFrame();

    if(pointer_hover&&!open&&!kbd) {
        float s=fminf(io.DisplaySize.x/1280.f,io.DisplaySize.y/720.f);
        ImVec2 a(io.DisplaySize.x*.5f+pointer_box[0]*s,io.DisplaySize.y*.5f-pointer_box[3]*s);
        ImVec2 b(io.DisplaySize.x*.5f+pointer_box[2]*s,io.DisplaySize.y*.5f-pointer_box[1]*s);
        ImGui::GetForegroundDrawList()->AddRect(a,b,IM_COL32(250,250,220,255),2.f*s,0,2.f*s);
    }

    extern volatile int g_photo_on;  // (runtime/photo.c)
    if (g_settings.perf_overlay != PERF_OFF && !g_photo_on) perf_overlay_draw(io, scale);
    if (title) {  // the title screen's brand, where the game's version line was (fs.c leaves it blank)
        bool up = tt && SDL_GetTicks() - tt < 300 && !open;  // (hidden under the open menu)
        title_a += ((up ? 1.0f : 0.0f) - title_a) * fminf(1.0f, io.DeltaTime * 6);
        float W = io.DisplaySize.x, H = io.DisplaySize.y, fh = H * 0.0185f, pitch = H * 0.0285f, xr = W * 0.9553f;
        ImDrawList *dl = ImGui::GetBackgroundDrawList();
        ImFont *f1 = g_menu_font_big ? g_menu_font_big : g_font, *f2 = g_menu_font ? g_menu_font : g_font;
        const char *l1 = slop_title(), *l2 = "a fork of Savage Wind";
        ImVec2 s1 = f1->CalcTextSizeA(fh, FLT_MAX, 0, l1), s2 = f2->CalcTextSizeA(fh * 0.8f, FLT_MAX, 0, l2);
        float y1 = H * 0.9142f - 2 * pitch, y2 = H * 0.9142f - pitch + fh * 0.1f;  // (clear of the line the game keeps for the DLC version)
        int a = (int)(255 * title_a), sh = (int)(110 * title_a);
        float o = fmaxf(1.0f, H / 1080.0f);
        dl->AddText(f1, fh, ImVec2(xr - s1.x + o, y1 + o), IM_COL32(0, 0, 0, sh), l1);
        dl->AddText(f1, fh, ImVec2(xr - s1.x, y1), IM_COL32(246, 244, 236, a), l1);
        dl->AddText(f2, fh * 0.8f, ImVec2(xr - s2.x + o, y2 + o), IM_COL32(0, 0, 0, sh), l2);
        dl->AddText(f2, fh * 0.8f, ImVec2(xr - s2.x, y2), IM_COL32(190, 226, 84, a), l2);
    }
    ach_ui_toasts(io.DisplaySize.x, io.DisplaySize.y, scale);

    if (open) {
        // dim the game behind the menu
        {   // dim and tint the game behind the menu, a vignette toward the edges
            ImDrawList *bg = ImGui::GetBackgroundDrawList();
            bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(2, 8, 14, 150));
            float ew = io.DisplaySize.x * 0.18f;
            bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ew, io.DisplaySize.y), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120));
            bg->AddRectFilledMultiColor(ImVec2(io.DisplaySize.x - ew, 0), io.DisplaySize, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 0));
        }
        float w = fminf(io.DisplaySize.x * 0.9f, 1500 * scale), h = io.DisplaySize.y - 2 * 18 * scale;  // (all the height: a slim margin)
        g_open_a += (1 - g_open_a) * fminf(1.0f, io.DeltaTime * 10);
        float ease = g_open_a > 0.999f ? 1 : g_open_a;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.15f + 0.85f * ease);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f + (1 - ease) * 40 * scale), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::Begin("##menu", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImFont *keep_big = g_font_big;  // (the window in the menu's font; header() uses g_font_big)
        g_font_big = g_menu_font_big;
        ImGui::PushFont(g_menu_font);
        // header as on the start screen's Settings: the title, and a close X in the corner (B / Esc too). ("Title
        // screen" - rt_request_reboot(RT_REBOOT_TITLE) - is to find a new home; user 2026-10-11.)
        ImGui::PushFont(g_font_big);
        ImGui::TextColored(ImVec4(0.745f, 0.886f, 0.33f, 1), "%s", tr("Settings"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", slop_title());
        ImGui::PopFont();
        {
            float bx = 44 * scale;
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bx);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
            bool close = ImGui::InvisibleButton("##close", ImVec2(bx, bx), ImGuiButtonFlags_EnableNav);
            ImGui::PopStyleColor();
            bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
            ImU32 col = hot ? IM_COL32(190, 226, 84, 255) : IM_COL32(205, 215, 220, 255);
            float m = bx * 0.3f, t = (hot ? 3.0f : 2.4f) * scale * 1.5f;
            ImDrawList *dl = ImGui::GetWindowDrawList();
            if (hot) dl->AddRectFilled(p, ImVec2(p.x + bx, p.y + bx), IM_COL32(255, 255, 255, 20), 8 * scale);
            dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + bx - m, p.y + bx - m), col, t);
            dl->AddLine(ImVec2(p.x + bx - m, p.y + m), ImVec2(p.x + m, p.y + bx - m), col, t);
            if (close) set_open(false);
        }

        bool any_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId), typing = ui_text_busy();  // (before the
        tabs_body(scale, true, NULL);                                                                          //  pages run)
        // B with nothing focused-deeper closes the menu (not the Esc / B that ends typing: typing is from the frame
        // before, so it's still true the frame a field lets go)
        if (!any_popup && !typing && !ui_text_busy() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            set_open(false);
        ImGui::PopFont();
        g_font_big = keep_big;
        ImGui::End();
        ImGui::PopStyleVar();
        osk_draw(g_font_big, scale);  // (a text field activated with the controller)
    }

    if (kbd) keyboard_draw(io, scale);
    if (sizes) size_list_draw(io, scale);
    ImGui::Render();
    g_text_busy = open && ui_text_busy();
#ifdef __APPLE__
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
    [enc endEncoding];
#else
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), (VkCommandBuffer)cmd);  // inside the window image's rendering
#endif
}
