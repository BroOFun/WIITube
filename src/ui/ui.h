/* WiiTube's YouTube-style interface: shared helpers (text, shapes) and the screens. All of
 * it is portable; the platform supplies the drawing backend (draw.h) and input. */
#ifndef WIITUBE_UI_H
#define WIITUBE_UI_H

#include "../core/image.h"
#include "../core/yt.h"
#include "atlas.h"
#include "draw.h"

/* ---- colours (YouTube dark theme) ---- */
#define C_BG        0x0f0f0fffu
#define C_TEXT      0xf1f1f1ffu
#define C_TEXT2     0xaaaaaaffu
#define C_CHIP      0x272727ffu
#define C_CHIP_SEL  0xf1f1f1ffu
#define C_FIELD     0x121212ffu
#define C_BORDER    0x303030ffu
#define C_BUTTON    0x222222ffu
#define C_RED       0xff0000ffu
#define C_BLUE      0x3ea6ffffu
#define C_PANEL     0x212121ffu
#define C_KEY       0x3a3a3affu
#define C_FOCUS     0xffffffffu

/* ---- input ---- */
enum {
    UI_UP = 1 << 0, UI_DOWN = 1 << 1, UI_LEFT = 1 << 2, UI_RIGHT = 1 << 3,
    UI_A = 1 << 4, UI_B = 1 << 5, UI_PLUS = 1 << 6, UI_MINUS = 1 << 7,
    UI_HOME = 1 << 8, UI_1 = 1 << 9, UI_2 = 1 << 10,
};
#define UI_DIRS (UI_UP | UI_DOWN | UI_LEFT | UI_RIGHT)

typedef struct {
    unsigned pressed;    /* buttons pressed this frame (with D-pad auto-repeat) */
    int pointer;         /* Wii Remote is pointing at the screen */
    float px, py;        /* pointer position (virtual canvas) */
    int pointer_moved;   /* pointer moved noticeably this frame */
} ui_input;

/* ---- text ---- */
/* Convert UTF-8 to the atlas character set (ASCII + bullet); accents lose their marks,
 * emoji are dropped. */
void ui_ascii(char *dst, size_t dstlen, const char *utf8);
float ui_text_width(int font, const char *ascii);
/* Draw single-line text with its top at y. Returns the width drawn. Text wider than max_w
 * (if > 0) is cut with "...". */
float ui_text(int font, float x, float y, const char *ascii, rgba_t c, float max_w);
/* Word-wrap into at most max_lines lines (the last one gets "..." if cut); returns the
 * number of lines drawn. */
int ui_text_wrap(int font, float x, float y, const char *ascii, rgba_t c, float max_w, int max_lines);
int ui_text_wrap_center(int font, float cx, float y, const char *ascii, rgba_t c, float max_w, int max_lines);
int ui_line_height(int font);

/* ---- shapes ---- */
void ui_round_rect(float x, float y, float w, float h, float r, rgba_t c);
/* Round the corners of a rectangle drawn earlier by painting the outside of each corner
 * in the background colour bg. */
void ui_round_corners(float x, float y, float w, float h, float r, rgba_t bg);
void ui_circle(float cx, float cy, float r, rgba_t c);
void ui_icon(int icon, float x, float y, float w, float h, rgba_t c);
void ui_spinner(float cx, float cy, float r, unsigned frame);
void ui_cursor(float x, float y);
rgba_t ui_alpha(rgba_t c, float a); /* scale a colour's alpha */

/* ---- screens ---- */
typedef const wt_image *(*ui_thumb_fn)(void *ud, const char *video_id);

#define UI_MAX_RESULTS 40
#define UI_NUM_CHIPS 8
extern const char *const ui_chips[UI_NUM_CHIPS];

enum { FOCUS_SEARCH, FOCUS_CHIPS, FOCUS_GRID };

typedef struct {
    char query[64];            /* text shown in the search box */
    int chip;                  /* highlighted topic chip, -1 when showing a typed search */
    yt_search_result results[UI_MAX_RESULTS];
    int nresults;
    int sel, chip_sel, focus;
    float scroll, chip_scroll; /* animated */
    int quality;               /* 144/240/360/480 */
    const char *status;        /* message shown in place of the grid (loading / errors) */
    int loading;
    const wt_image *logo;
    unsigned frame;
} browse_state;

enum {
    ACT_NONE, ACT_OPEN_KEYBOARD, ACT_CHIP, ACT_PLAY, ACT_QUALITY, ACT_QUIT,
    ACT_KB_SEARCH, ACT_KB_CANCEL, ACT_PLAYER_BACK, ACT_PLAYER_PAUSE, ACT_PLAYER_VOLUME,
};

void browse_init(browse_state *b, const wt_image *logo);
int browse_update(browse_state *b, const ui_input *in);   /* returns ACT_* */
void browse_draw(browse_state *b, ui_thumb_fn thumb, void *ud);

typedef struct {
    char text[64];
    int row, col;
    unsigned frame;
} keyboard_state;

void keyboard_open(keyboard_state *k, const char *text);
int keyboard_update(keyboard_state *k, const ui_input *in);
void keyboard_draw(keyboard_state *k);

typedef struct {
    char title[256], channel[128];
    double position, duration;  /* seconds */
    float buffered;             /* 0..1 of the video downloaded */
    int paused, buffering, volume, quality;
    int overlay_timer;          /* frames left to show the controls */
    int volume_timer;
    unsigned frame;
} player_view;

/* Handles the player's buttons; returns ACT_PLAYER_* (or ACT_NONE). */
int player_update(player_view *p, const ui_input *in);
/* Draw the controls over the video (the video itself is drawn first by the caller). */
void player_draw_overlay(player_view *p);
/* Fit a w x h picture into the screen keeping its shape. */
void ui_fit(int w, int h, float *x, float *y, float *dw, float *dh);

/* Full-screen message with the logo: spinner if busy, else an error with "Press A". */
void ui_message(const wt_image *logo, const char *title, const char *detail, int busy, unsigned frame);

#endif
