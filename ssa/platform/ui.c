/* Platform UI abstraction — raylib wrappers for SSA extern calls */

#include "ui.h"

#include <stddef.h>

#include "raylib.h"

static Font ui_font;
static int ui_font_loaded = 0;

static Color rgb_to_color(int rgb) {
    Color c;
    c.r = (unsigned char)((rgb >> 16) & 0xFF);
    c.g = (unsigned char)((rgb >> 8) & 0xFF);
    c.b = (unsigned char)(rgb & 0xFF);
    c.a = 255;
    return c;
}

static float ui_spacing(int size) {
    return (float)size / 10.0f;
}

int c1_ui_init(int width, int height, const char *title) {
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(width, height, title);
    if (!IsWindowReady()) return -1;
    SetTargetFPS(60);
    ui_font = GetFontDefault();
    ui_font_loaded = 0;
    return 0;
}

void c1_ui_close(void) {
    if (ui_font_loaded) UnloadFont(ui_font);
    CloseWindow();
}

int c1_ui_should_close(void) {
    return WindowShouldClose() ? 1 : 0;
}

int c1_ui_begin_frame(void) {
    if (WindowShouldClose()) return 0;
    BeginDrawing();
    return 1;
}

void c1_ui_end_frame(void) {
    EndDrawing();
}

void c1_ui_clear(int rgb) {
    ClearBackground(rgb_to_color(rgb));
}

void c1_ui_set_fps(int fps) {
    SetTargetFPS(fps);
}

void c1_ui_rect(int x, int y, int w, int h, int rgb) {
    DrawRectangleRec((Rectangle){ (float)x, (float)y, (float)w, (float)h },
                     rgb_to_color(rgb));
}

void c1_ui_rect_rounded(int x, int y, int w, int h, int radius, int rgb) {
    float min_side = (float)(w < h ? w : h);
    float roundness = 0.0f;
    if (min_side > 0.0f) roundness = (float)radius / (min_side / 2.0f);
    if (roundness > 1.0f) roundness = 1.0f;
    if (roundness <= 0.0f) {
        DrawRectangleRec((Rectangle){ (float)x, (float)y, (float)w, (float)h },
                         rgb_to_color(rgb));
        return;
    }
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h },
                         roundness, 8, rgb_to_color(rgb));
}

void c1_ui_text(const char *text, int x, int y, int size, int rgb) {
    DrawTextEx(ui_font, text, (Vector2){ (float)x, (float)y }, (float)size,
               ui_spacing(size), rgb_to_color(rgb));
}

int c1_ui_text_width(const char *text, int size) {
    return (int)MeasureTextEx(ui_font, text, (float)size, ui_spacing(size)).x;
}

int c1_ui_load_font(const char *path, int size) {
    Font f = LoadFontEx(path, size, NULL, 0);
    if (!IsFontValid(f)) return -1;
    if (ui_font_loaded) UnloadFont(ui_font);
    ui_font = f;
    ui_font_loaded = 1;
    SetTextureFilter(ui_font.texture, TEXTURE_FILTER_BILINEAR);
    return 0;
}

int c1_ui_mouse_x(void) {
    return GetMouseX();
}

int c1_ui_mouse_y(void) {
    return GetMouseY();
}

int c1_ui_mouse_down(void) {
    return IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? 1 : 0;
}

int c1_ui_mouse_pressed(void) {
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ? 1 : 0;
}

int c1_ui_char_pressed(void) {
    int ch = GetCharPressed();
    return ch > 0 ? ch : 0;
}

int c1_ui_key_pressed(int key) {
    int rk;
    switch (key) {
        case C1_UI_KEY_BACKSPACE: rk = KEY_BACKSPACE; break;
        case C1_UI_KEY_ENTER:     rk = KEY_ENTER;     break;
        case C1_UI_KEY_LEFT:      rk = KEY_LEFT;      break;
        case C1_UI_KEY_RIGHT:     rk = KEY_RIGHT;     break;
        case C1_UI_KEY_TAB:       rk = KEY_TAB;       break;
        default: return 0;
    }
    return IsKeyPressed(rk) ? 1 : 0;
}

void c1_ui_screenshot(const char *path) {
    TakeScreenshot(path);
}
