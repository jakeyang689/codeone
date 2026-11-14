/* Platform UI abstraction — raylib wrappers for SSA extern calls.
 *
 * Only the window, the frame loop and flat drawing calls live here. The
 * widget tree, layout and hit testing are written in SSA (boxes/std/ui).
 *
 * Everything is integer pixels: SSA has no float <-> integer conversion
 * instructions, and the layout engine works in whole pixels anyway.
 * Colors are 0xRRGGBB; alpha is always opaque.
 */

#ifndef C1_UI_H
#define C1_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Window + frame loop. c1_ui_init returns 0 on success. */
int  c1_ui_init(int width, int height, const char *title);
void c1_ui_close(void);
int  c1_ui_should_close(void);
int  c1_ui_begin_frame(void);  /* 0 once the window is closing */
void c1_ui_end_frame(void);
void c1_ui_clear(int rgb);
void c1_ui_set_fps(int fps);

/* Drawing. radius is clamped to half the shorter side. */
void c1_ui_rect(int x, int y, int w, int h, int rgb);
void c1_ui_rect_rounded(int x, int y, int w, int h, int radius, int rgb);
void c1_ui_text(const char *text, int x, int y, int size, int rgb);
int  c1_ui_text_width(const char *text, int size);

/* Optional TTF font; falls back to the built-in bitmap font. */
int c1_ui_load_font(const char *path, int size);

/* Input. */
int c1_ui_mouse_x(void);
int c1_ui_mouse_y(void);
int c1_ui_mouse_down(void);
int c1_ui_mouse_pressed(void);

/* Keyboard. Keys use the small id set the SSA widgets need. */
#define C1_UI_KEY_BACKSPACE 1
#define C1_UI_KEY_ENTER     2
#define C1_UI_KEY_LEFT      3
#define C1_UI_KEY_RIGHT     4
#define C1_UI_KEY_TAB       5
int c1_ui_char_pressed(void);   /* next queued character, 0 when empty */
int c1_ui_key_pressed(int key); /* 1 on the frame the key went down */

/* Writes the current framebuffer to a PNG. */
void c1_ui_screenshot(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* C1_UI_H */
