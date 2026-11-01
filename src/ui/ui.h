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
    UI_C = 1 << 11, UI_Z = 1 << 12, /* Nunchuk */
};
#define UI_DIRS (UI_UP | UI_DOWN | UI_LEFT | UI_RIGHT)

typedef struct {
    unsigned pressed;    /* buttons pressed this frame (with D-pad / stick auto-repeat) */
    unsigned held;       /* buttons held down */
    int pointer;         /* Wii Remote is pointing at the screen */
    float px, py;        /* pointer position (virtual canvas) */
    int pointer_moved;   /* pointer moved noticeably this frame */
    float stick_x, stick_y; /* Nunchuk stick, -1..1 (up = negative y), 0 without a Nunchuk */
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
/* Thumbnail lookup: vertical = the 9:16 picture wanted (Shorts). */
typedef const wt_image *(*ui_thumb_fn)(void *ud, const yt_search_result *r, int vertical);

#define UI_NUM_CHIPS 8
extern const char *const ui_chips[UI_NUM_CHIPS];

enum { PAGE_HOME, PAGE_SHORTS, PAGE_SEARCH, PAGE_COUNT };
enum { FOCUS_TOP, FOCUS_RAIL, FOCUS_CHIPS, FOCUS_GRID };
enum { TOP_LOGO, TOP_SEARCH, TOP_QUALITY };

/* One page of results. Home and Search show regular videos with a Shorts shelf; the Shorts
 * page shows a grid of Shorts. The feeds are filled by the app (in a background job that
 * hands complete pages over on the main thread). */
typedef struct {
    yt_feed videos, shorts;
    int loading_videos, loading_shorts; /* a page is on its way */
    int failed;                         /* last load failed (stops automatic retries) */
    char status[256];                   /* message shown when there are no results */
    int chip;                           /* topic (Home, Shorts) */
    char query[64];                     /* Search */
    int started;                        /* bit 1: videos requested, bit 2: Shorts requested */
    int gen;                            /* changes when the page is reset (stale loads are dropped) */
    float scroll, scroll_to;            /* vertical scroll (animated towards scroll_to) */
    int row, col;                       /* selection: row in the layout, column */
    int shelf_sel;                      /* selected Short in the shelf */
    float shelf_scroll, shelf_to;       /* first visible Short (animated) */
} browse_page;

typedef struct {
    int page;                  /* PAGE_* */
    browse_page pages[PAGE_COUNT];
    int focus, top_sel, rail_sel, chip_sel;
    float chip_scroll;
    int quality;               /* 144/240/360/480 */
    const wt_image *logo;
    unsigned frame;
    int dragging;              /* scrollbar thumb held with A */
    float drag_dy;
    int hover_kind, hover_index; /* what the pointer was on (for highlighting) */
    /* Set by browse_update: */
    int want_videos, want_shorts;   /* the current page should load more (or its first page) */
    const yt_feed *play_feed;       /* ACT_PLAY: the list and position to play */
    int play_index, play_short;
} browse_state;

enum {
    ACT_NONE, ACT_OPEN_KEYBOARD, ACT_CHIP, ACT_PAGE, ACT_PLAY, ACT_QUALITY, ACT_QUIT,
    ACT_KB_SEARCH, ACT_KB_CANCEL,
    ACT_PLAYER_BACK, ACT_PLAYER_PAUSE, ACT_PLAYER_VOLUME, ACT_PLAYER_SEEK, ACT_PLAYER_CAPTIONS,
    ACT_PLAYER_SET_QUALITY, ACT_PLAYER_SET_AUDIO, ACT_PLAYER_NEXT, ACT_PLAYER_PREV,
};

void browse_init(browse_state *b, const wt_image *logo);
/* Forget a page's results (new topic or search); its next update asks for the first page. */
void browse_reset_page(browse_state *b, int page);
/* Switch page (keeps each page's results). */
void browse_set_page(browse_state *b, int page);
int browse_update(browse_state *b, const ui_input *in);   /* returns ACT_* */
/* After watching Shorts: select Short number i of the current page (shelf or grid). */
void browse_select_short(browse_state *b, int i);
void browse_draw(browse_state *b, ui_thumb_fn thumb, void *ud);

typedef struct {
    char text[64];
    int row, col;
    int shift;
    unsigned frame;
} keyboard_state;

void keyboard_open(keyboard_state *k, const char *text);
int keyboard_update(keyboard_state *k, const ui_input *in);
void keyboard_draw(keyboard_state *k);

/* ---- player ---- */
#define PV_MAX_AUDIO 8
#define PV_MAX_QUALITY 4

enum { MENU_CLOSED, MENU_MAIN, MENU_QUALITY, MENU_CAPTIONS, MENU_AUDIO };

typedef struct {
    char title[256], channel[128];
    int is_short;               /* vertical Shorts layout (Up/Down = previous/next) */
    double position, duration;  /* seconds */
    float buffered;             /* 0..1: how far the download has got */
    int paused, buffering, volume, quality;
    int overlay_timer;          /* frames left to show the controls */
    int volume_timer;
    unsigned frame;
    /* Seeking: a pending jump (from the D-pad / stick, accumulated) or a click on the bar. */
    double seek_to;             /* target of ACT_PLAYER_SEEK */
    double nudge_target;        /* >= 0 while D-pad presses are being collected */
    double nudge_from;
    int nudge_timer, stick_timer;
    float hover_bar;            /* 0..1 pointer position over the progress bar, < 0 none */
    int can_seek;
    char notice[64];            /* short message in the middle ("Captions on") */
    int notice_timer;
    /* Captions. */
    int cc_available, cc_on;
    char cc_label[64];          /* "English (auto-generated)" */
    char cc_text[512];          /* current caption (ASCII, may contain '\n') */
    /* Settings menu (button 2 or the gear). */
    int menu, menu_sel;
    int qualities[PV_MAX_QUALITY], nqualities; /* choices offered (e.g. 144..480) */
    int want_quality;           /* ACT_PLAYER_SET_QUALITY */
    char audio_names[PV_MAX_AUDIO][64];
    int naudio, audio_sel, want_audio; /* ACT_PLAYER_SET_AUDIO */
    int has_next, has_prev;     /* Shorts */
} player_view;

void player_view_init(player_view *p);
/* Handles the player's buttons; returns ACT_PLAYER_* (or ACT_NONE). */
int player_update(player_view *p, const ui_input *in);
/* Draw the controls over the video (the video itself is drawn first by the caller). */
void player_draw_overlay(player_view *p);
/* Where the video goes: fitted to the screen (or the Shorts column). */
void player_video_rect(const player_view *p, int w, int h, float *x, float *y, float *dw, float *dh);
/* Fit a w x h picture into the screen keeping its shape. */
void ui_fit(int w, int h, float *x, float *y, float *dw, float *dh);

/* Full-screen message with the logo: spinner if busy, else an error with "Press A". */
void ui_message(const wt_image *logo, const char *title, const char *detail, int busy, unsigned frame);

#endif
