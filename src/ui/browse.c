/* Browse screens: Home (chips: For you = YouTube's recommendations, then topics) and search
 * results, both in YouTube's order with Shorts shelves where YouTube puts them. A left rail
 * has Home, Shorts (opens the Shorts player) and Search; a scrollbar on the right can be
 * dragged with the Wii Remote pointer. Lists grow as you scroll (the app loads the next
 * page when browse_update sets want_more). */
#include "ui.h"

#include <stdio.h>
#include <string.h>

const char *const ui_topics[UI_NUM_TOPICS] = {
    "Gaming", "Music", "Wii", "Mario Kart", "Minecraft", "Comedy", "Sports", "Podcasts",
};

/* ---- layout ------------------------------------------------------------------------ */

#define TOP_H       52
#define RAIL_W      76
#define CHIP_Y      60
#define CHIP_H      30
#define GRID_Y      104
#define GAP_X       14
#define GAP_Y       18
#define GAP_S       10
#define THUMB_R     8
#define SB_HIT      26   /* scrollbar: width of the area that reacts to the pointer */
#define SHELF_HEAD  34
#define FOOTER_H    64
#define MAX_ROWS    400

typedef struct {
    float x, y, w, h;
} box;

static int inside(const box *b, float x, float y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static box mkbox(float x, float y, float w, float h)
{
    box b;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    return b;
}

static float content_x(void) { return RAIL_W + 8; }
static float content_w(void) { return ui_width() - content_x() - SB_HIT - 4; }
static float view_h(void) { return UI_H - GRID_Y; }
static int video_cols(void) { return ui_width() >= 800 ? 4 : 3; }
static int short_cols(void) { return ui_width() >= 800 ? 6 : 5; }

static float card_w(void)
{
    const int c = video_cols();
    return (float)(int)((content_w() - (c - 1) * GAP_X) / c);
}

static float thumb_h(void) { return (float)(int)(card_w() * 9 / 16); }

static float card_h(void)
{
    return thumb_h() + 10 + 2 * ui_line_height(FONT_TITLE) + 2 + 2 * ui_line_height(FONT_META);
}

static float short_w(void)
{
    const int c = short_cols();
    return (float)(int)((content_w() - (c - 1) * GAP_S) / c);
}

static float short_th(void) { return (float)(int)(short_w() * 16 / 9); }
static float short_h(void) { return short_th() + 8 + 2 * ui_line_height(FONT_TITLE) + ui_line_height(FONT_META); }

enum { ROW_VIDEOS, ROW_SHELF, ROW_FOOTER };

typedef struct {
    int type, first, count; /* entries first .. first+count-1 of the list */
    int shelf;              /* ROW_SHELF: shelf number */
    float y, h;             /* in content coordinates (0 = top of the list) */
} row_t;

static row_t g_rows[MAX_ROWS];

/* The page as rows, in list order: runs of videos in rows of video_cols(), each Shorts
 * shelf as one horizontal row; a footer while more can load. */
static int layout(const browse_page *pg, float *total)
{
    const yt_feed *f = &pg->feed;
    const int c = video_cols();
    int n = 0, i = 0;
    float y = 0;
    while (i < f->n && n < MAX_ROWS - 1) {
        row_t *r = &g_rows[n++];
        int k = i;
        r->y = y;
        if (f->items[i].shelf > 0) {
            while (k < f->n && f->items[k].shelf == f->items[i].shelf)
                k++;
            r->type = ROW_SHELF;
            r->shelf = f->items[i].shelf;
            r->h = SHELF_HEAD + short_h() + GAP_Y;
        } else {
            while (k < f->n && k - i < c && f->items[k].shelf == 0)
                k++;
            r->type = ROW_VIDEOS;
            r->shelf = 0;
            r->h = card_h() + GAP_Y;
        }
        r->first = i;
        r->count = k - i;
        y += r->h;
        i = k;
    }
    if (f->n > 0 && (pg->loading || f->continuation || pg->failed)) {
        row_t *r = &g_rows[n++];
        r->type = ROW_FOOTER;
        r->first = r->count = r->shelf = 0;
        r->y = y;
        r->h = FOOTER_H;
        y += FOOTER_H;
    }
    *total = y;
    return n;
}

static float max_scroll(float total)
{
    const float m = total - view_h() + 8;
    return m > 0 ? m : 0;
}

/* Index of the last selectable row (not the footer). */
static int last_row(int n)
{
    return n > 0 && g_rows[n - 1].type == ROW_FOOTER ? n - 2 : n - 1;
}

static int slot(int shelf) { return shelf % UI_SHELF_SLOTS; }

static box video_box(const browse_page *pg, const row_t *r, int col)
{
    return mkbox(content_x() + col * (card_w() + GAP_X), GRID_Y + r->y - pg->scroll, card_w(), card_h());
}

/* Short number i (0 = first of the shelf) in a shelf row. */
static box shelf_box(const browse_page *pg, const row_t *r, int i)
{
    return mkbox(content_x() + (i - pg->shelf_scroll[slot(r->shelf)]) * (short_w() + GAP_S),
                 GRID_Y + r->y + SHELF_HEAD - pg->scroll, short_w(), short_h());
}

static int shelf_can_left(const browse_page *pg, const row_t *r) { return pg->shelf_to[slot(r->shelf)] > 0.01f; }

static int shelf_can_right(const browse_page *pg, const row_t *r)
{
    return pg->shelf_to[slot(r->shelf)] + short_cols() < r->count;
}

static box shelf_arrow(const browse_page *pg, const row_t *r, int right)
{
    const float cy = GRID_Y + r->y + SHELF_HEAD - pg->scroll + short_th() / 2;
    const float cx = right ? content_x() + content_w() - 18 : content_x() + 18;
    return mkbox(cx - 18, cy - 18, 36, 36);
}

static box logo_box(const browse_state *b)
{
    float h = 30, w = b->logo ? b->logo->w * h / b->logo->h : 100;
    return mkbox(14, 11, w + 4, h);
}

static box search_box(void)
{
    float w = ui_width() * 0.46f;
    if (w > 440)
        w = 440;
    return mkbox((float)(int)((ui_width() - w) / 2), 9, w, 34);
}

static box quality_box(const browse_state *bs)
{
    char label[32];
    box b;
    snprintf(label, sizeof label, "%dp", bs->quality);
    b.w = ui_text_width(FONT_TITLE, label) + ui_text_width(FONT_META, "Quality ") + 24;
    b.h = 28;
    b.x = ui_width() - 16 - b.w;
    b.y = 12;
    return b;
}

static box rail_box(int i) { return mkbox(6, GRID_Y - 40 + i * 70, RAIL_W - 12, 62); }

/* ---- chips (Home) ---- */

static int chip_count(const browse_state *b) { return UI_NUM_TOPICS + (b->has_for_you ? 1 : 0); }

const char *browse_chip_topic(const browse_state *b, int i)
{
    if (b->has_for_you) {
        if (i == 0)
            return NULL;
        i--;
    }
    return ui_topics[i >= 0 && i < UI_NUM_TOPICS ? i : 0];
}

static const char *chip_label(const browse_state *b, int i)
{
    const char *t = browse_chip_topic(b, i);
    return t ? t : "For you";
}

static float chip_w(const browse_state *b, int i) { return ui_text_width(FONT_TITLE, chip_label(b, i)) + 24; }

static box chip_box(const browse_state *bs, int i)
{
    box b;
    int k;
    b.x = content_x() - bs->chip_scroll;
    for (k = 0; k < i; k++)
        b.x += chip_w(bs, k) + 10;
    b.y = CHIP_Y;
    b.w = chip_w(bs, i);
    b.h = CHIP_H;
    return b;
}

static int has_chips(int page) { return page == PAGE_HOME; }

/* Scrollbar track and thumb. */
static void scrollbar(const browse_page *pg, float total, box *track, box *thumb)
{
    const float ms = max_scroll(total);
    *track = mkbox(ui_width() - SB_HIT, GRID_Y, SB_HIT, view_h() - 8);
    thumb->x = track->x;
    thumb->w = track->w;
    thumb->h = total > 0 ? track->h * view_h() / (total + 8) : track->h;
    if (thumb->h < 36)
        thumb->h = 36;
    if (thumb->h > track->h)
        thumb->h = track->h;
    thumb->y = track->y + (ms > 0 ? pg->scroll / ms : 0) * (track->h - thumb->h);
}

/* ---- state ------------------------------------------------------------------------- */

void browse_init(browse_state *b, const wt_image *logo)
{
    int i;
    memset(b, 0, sizeof *b);
    b->logo = logo;
    b->quality = 480;
    b->focus = FOCUS_GRID;
    for (i = 0; i < PAGE_COUNT; i++)
        b->pages[i].gen = 1;
}

void browse_reset_page(browse_state *b, int page)
{
    browse_page *pg = &b->pages[page];
    const int chip = pg->chip, gen = pg->gen;
    char query[64];
    memcpy(query, pg->query, sizeof query);
    yt_feed_free(&pg->feed);
    memset(pg, 0, sizeof *pg);
    pg->chip = chip;
    pg->gen = gen + 1;
    memcpy(pg->query, query, sizeof query);
}

void browse_set_page(browse_state *b, int page)
{
    b->page = page;
    b->rail_sel = page == PAGE_SEARCH ? RAIL_SEARCH : RAIL_HOME;
    b->chip_sel = b->pages[page].chip;
    b->chip_scroll = 0;
    b->dragging = 0;
}

/* Keep the selected row on screen (D-pad navigation). */
static void scroll_to_row(browse_page *pg, int row, float total)
{
    const row_t *r = &g_rows[row];
    float s = pg->scroll_to;
    if (r->y < s)
        s = r->y;
    if (r->y + r->h - GAP_Y > s + view_h() - 8)
        s = r->y + r->h - GAP_Y - (view_h() - 8);
    if (s > max_scroll(total))
        s = max_scroll(total);
    pg->scroll_to = s < 0 ? 0 : s;
}

/* Scroll a shelf so that its Short number col is visible. */
static void shelf_follow(browse_page *pg, const row_t *r, int col)
{
    const int vis = short_cols(), s = slot(r->shelf);
    if (col < pg->shelf_to[s])
        pg->shelf_to[s] = (float)col;
    if (col >= pg->shelf_to[s] + vis)
        pg->shelf_to[s] = (float)(col - vis + 1);
}

static void shelf_page(browse_page *pg, const row_t *r, int dir)
{
    const int vis = short_cols(), s = slot(r->shelf);
    float t = pg->shelf_to[s] + dir * (vis - 1);
    if (t > r->count - vis)
        t = (float)(r->count - vis);
    if (t < 0)
        t = 0;
    pg->shelf_to[s] = (float)(int)t;
}

static void keep_chip_visible(browse_state *b)
{
    box c = chip_box(b, b->chip_sel);
    const float right = content_x() + content_w();
    if (c.x < content_x())
        b->chip_scroll -= content_x() - c.x;
    if (c.x + c.w > right)
        b->chip_scroll += c.x + c.w - right;
}

/* What the pointer is on. */
enum {
    HIT_NONE, HIT_LOGO, HIT_SEARCH, HIT_QUALITY, HIT_RAIL, HIT_CHIP, HIT_CARD, HIT_SHELF_LEFT, HIT_SHELF_RIGHT,
    HIT_SB_THUMB, HIT_SB_TRACK, HIT_CONTENT,
};

typedef struct {
    int kind, index, row, col;
} hit_t;

static hit_t hit_test(const browse_state *b, const browse_page *pg, int nrows, float total, float x, float y)
{
    hit_t h;
    box bx, track, thumb;
    int i, r;
    memset(&h, 0, sizeof h);
    bx = logo_box(b);
    if (inside(&bx, x, y)) {
        h.kind = HIT_LOGO;
        return h;
    }
    bx = search_box();
    if (inside(&bx, x, y)) {
        h.kind = HIT_SEARCH;
        return h;
    }
    bx = quality_box(b);
    if (inside(&bx, x, y)) {
        h.kind = HIT_QUALITY;
        return h;
    }
    if (x < RAIL_W) {
        for (i = 0; i < RAIL_COUNT; i++) {
            bx = rail_box(i);
            if (inside(&bx, x, y)) {
                h.kind = HIT_RAIL;
                h.index = i;
            }
        }
        return h;
    }
    if (has_chips(b->page) && y >= CHIP_Y && y < CHIP_Y + CHIP_H) {
        for (i = 0; i < chip_count(b); i++) {
            bx = chip_box(b, i);
            if (inside(&bx, x, y) && x >= content_x() - 4) {
                h.kind = HIT_CHIP;
                h.index = i;
            }
        }
        return h;
    }
    if (y < GRID_Y - 6)
        return h;
    scrollbar(pg, total, &track, &thumb);
    if (max_scroll(total) > 0 && x >= track.x) {
        h.kind = inside(&thumb, x, y) ? HIT_SB_THUMB : HIT_SB_TRACK;
        return h;
    }
    h.kind = HIT_CONTENT;
    for (r = 0; r < nrows; r++) {
        const row_t *row = &g_rows[r];
        const float top = GRID_Y + row->y - pg->scroll;
        if (y < top || y >= top + row->h)
            continue;
        if (row->type == ROW_VIDEOS) {
            for (i = 0; i < row->count; i++) {
                bx = video_box(pg, row, i);
                if (inside(&bx, x, y)) {
                    h.kind = HIT_CARD;
                    h.row = r;
                    h.col = i;
                }
            }
        } else if (row->type == ROW_SHELF) {
            const int first = (int)pg->shelf_scroll[slot(row->shelf)];
            bx = shelf_arrow(pg, row, 0);
            if (shelf_can_left(pg, row) && inside(&bx, x, y)) {
                h.kind = HIT_SHELF_LEFT;
                h.row = r;
                return h;
            }
            bx = shelf_arrow(pg, row, 1);
            if (shelf_can_right(pg, row) && inside(&bx, x, y)) {
                h.kind = HIT_SHELF_RIGHT;
                h.row = r;
                return h;
            }
            for (i = first; i < row->count && i <= first + short_cols(); i++) {
                bx = shelf_box(pg, row, i);
                if (inside(&bx, x, y) && bx.x >= content_x() - 1 && bx.x + bx.w <= content_x() + content_w() + 1) {
                    h.kind = HIT_CARD;
                    h.row = r;
                    h.col = i;
                }
            }
        }
    }
    return h;
}

static int cycle_quality(browse_state *b)
{
    b->quality = b->quality == 144 ? 240 : b->quality == 240 ? 360 : b->quality == 360 ? 480 : 144;
    return ACT_QUALITY;
}

/* Play the selected entry. */
static int play_selected(browse_state *b, browse_page *pg, int nrows)
{
    const row_t *r;
    if (pg->row < 0 || pg->row >= nrows)
        return ACT_NONE;
    r = &g_rows[pg->row];
    if (r->type == ROW_FOOTER || pg->col < 0 || pg->col >= r->count)
        return ACT_NONE;
    b->play_feed = &pg->feed;
    b->play_index = r->first + pg->col;
    return ACT_PLAY;
}

static int go_rail(browse_state *b, int item)
{
    if (item == RAIL_SEARCH)
        return ACT_OPEN_KEYBOARD;
    if (item == RAIL_SHORTS)
        return ACT_SHORTS;
    if (b->page == PAGE_HOME) { /* already there: back to the top */
        b->pages[PAGE_HOME].scroll_to = 0;
        b->pages[PAGE_HOME].row = b->pages[PAGE_HOME].col = 0;
        b->focus = FOCUS_GRID;
        return ACT_NONE;
    }
    browse_set_page(b, PAGE_HOME);
    b->focus = FOCUS_GRID;
    return ACT_PAGE;
}

static int pick_chip(browse_state *b, int chip)
{
    browse_page *pg = &b->pages[b->page];
    b->chip_sel = chip;
    if (pg->chip == chip && pg->started && browse_chip_topic(b, chip))
        return ACT_NONE; /* (For you is refreshed every time it is picked) */
    pg->chip = chip;
    return ACT_CHIP;
}

void browse_select(browse_state *b, int i)
{
    browse_page *pg = &b->pages[b->page];
    float total;
    int n = layout(pg, &total), r;
    for (r = 0; r < n; r++) {
        const row_t *row = &g_rows[r];
        if (row->type == ROW_FOOTER || i < row->first || i >= row->first + row->count)
            continue;
        pg->row = r;
        pg->col = i - row->first;
        if (row->type == ROW_SHELF) {
            shelf_follow(pg, row, pg->col);
            pg->shelf_scroll[slot(row->shelf)] = pg->shelf_to[slot(row->shelf)];
        }
        b->focus = FOCUS_GRID;
        scroll_to_row(pg, r, total);
        pg->scroll = pg->scroll_to;
        return;
    }
}

/* Column in a row of to_cols that lines up with column col of a row of from_cols. */
static int map_col(int col, int from_cols, int to_cols)
{
    return (col * to_cols + from_cols / 2) / from_cols;
}

static void grid_keys(browse_state *b, browse_page *pg, unsigned k, int nrows, float total)
{
    const int last = last_row(nrows);
    const row_t *r;
    if (last < 0) {
        if (k & UI_UP)
            b->focus = has_chips(b->page) ? FOCUS_CHIPS : FOCUS_TOP;
        if (k & UI_LEFT)
            b->focus = FOCUS_RAIL;
        return;
    }
    if (pg->row > last)
        pg->row = last;
    if (pg->row < 0)
        pg->row = 0;
    r = &g_rows[pg->row];
    if (pg->col >= r->count)
        pg->col = r->count - 1;
    if (pg->col < 0)
        pg->col = 0;

    if (k & UI_RIGHT && pg->col + 1 < r->count) {
        pg->col++;
        if (r->type == ROW_SHELF)
            shelf_follow(pg, r, pg->col);
    }
    if (k & UI_LEFT) {
        if (pg->col > 0) {
            pg->col--;
            if (r->type == ROW_SHELF)
                shelf_follow(pg, r, pg->col);
        } else {
            b->focus = FOCUS_RAIL;
            b->rail_sel = b->page == PAGE_SEARCH ? RAIL_SEARCH : RAIL_HOME;
            return;
        }
    }
    if (k & (UI_DOWN | UI_UP)) {
        const int to = pg->row + ((k & UI_DOWN) ? 1 : -1);
        if (to < 0) {
            b->focus = has_chips(b->page) ? FOCUS_CHIPS : FOCUS_TOP;
            b->top_sel = TOP_SEARCH;
            pg->scroll_to = 0;
            return;
        }
        if (to <= last) {
            const row_t *nr = &g_rows[to];
            /* Keep the same horizontal position between grid rows and shelves. */
            int vis_col = pg->col, from_cols = video_cols(), to_cols = video_cols();
            if (r->type == ROW_SHELF) {
                vis_col -= (int)(pg->shelf_to[slot(r->shelf)] + 0.5f);
                from_cols = short_cols();
            }
            if (nr->type == ROW_SHELF)
                to_cols = short_cols();
            pg->col = map_col(vis_col, from_cols, to_cols);
            if (nr->type == ROW_SHELF)
                pg->col += (int)(pg->shelf_to[slot(nr->shelf)] + 0.5f);
            pg->row = to;
            if (pg->col >= nr->count)
                pg->col = nr->count - 1;
            if (pg->col < 0)
                pg->col = 0;
            if (nr->type == ROW_SHELF)
                shelf_follow(pg, nr, pg->col);
        }
    }
    if (k & UI_DIRS)
        scroll_to_row(pg, pg->row, total);
}

int browse_update(browse_state *b, const ui_input *in)
{
    browse_page *pg = &b->pages[b->page];
    const unsigned k = in->pressed;
    float total, ms;
    int nrows, i;
    hit_t h;

    b->frame++;
    b->want_more = 0;
    b->play_feed = NULL;
    nrows = layout(pg, &total);
    ms = max_scroll(total);
    memset(&h, 0, sizeof h);

    /* ---- pointer ---- */
    if (in->pointer)
        h = hit_test(b, pg, nrows, total, in->px, in->py);
    b->hover_kind = h.kind;
    b->hover_index = h.index;
    if (b->dragging) {
        if (!(in->held & UI_A) || !in->pointer) {
            b->dragging = 0;
        } else {
            box track, thumb;
            scrollbar(pg, total, &track, &thumb);
            if (track.h > thumb.h) {
                float f = (in->py - b->drag_dy - track.y) / (track.h - thumb.h);
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                pg->scroll_to = pg->scroll = f * ms;
            }
        }
    }
    if (in->pointer && in->pointer_moved && !b->dragging) {
        switch (h.kind) {
        case HIT_LOGO: b->focus = FOCUS_TOP; b->top_sel = TOP_LOGO; break;
        case HIT_SEARCH: b->focus = FOCUS_TOP; b->top_sel = TOP_SEARCH; break;
        case HIT_QUALITY: b->focus = FOCUS_TOP; b->top_sel = TOP_QUALITY; break;
        case HIT_RAIL: b->focus = FOCUS_RAIL; b->rail_sel = h.index; break;
        case HIT_CHIP: b->focus = FOCUS_CHIPS; b->chip_sel = h.index; break;
        case HIT_CARD: b->focus = FOCUS_GRID; pg->row = h.row; pg->col = h.col; break;
        default: break;
        }
    }

    /* ---- buttons ---- */
    if (k & UI_HOME)
        return ACT_QUIT;
    if (k & UI_PLUS)
        return ACT_OPEN_KEYBOARD;
    if (k & UI_1)
        return cycle_quality(b);
    if (k & UI_B) {
        if (b->page != PAGE_HOME)
            return go_rail(b, RAIL_HOME);
        pg->scroll_to = 0;
        pg->row = pg->col = 0;
        b->focus = FOCUS_GRID;
    }
    if (k & UI_A) {
        if (in->pointer) {
            switch (h.kind) {
            case HIT_LOGO: return go_rail(b, RAIL_HOME);
            case HIT_SEARCH: return ACT_OPEN_KEYBOARD;
            case HIT_QUALITY: return cycle_quality(b);
            case HIT_RAIL: return go_rail(b, h.index);
            case HIT_CHIP: return pick_chip(b, h.index);
            case HIT_CARD:
                pg->row = h.row;
                pg->col = h.col;
                return play_selected(b, pg, nrows);
            case HIT_SHELF_LEFT: shelf_page(pg, &g_rows[h.row], -1); break;
            case HIT_SHELF_RIGHT: shelf_page(pg, &g_rows[h.row], 1); break;
            case HIT_SB_THUMB: {
                box track, thumb;
                scrollbar(pg, total, &track, &thumb);
                b->dragging = 1;
                b->drag_dy = in->py - thumb.y;
                break;
            }
            case HIT_SB_TRACK: {
                box track, thumb;
                scrollbar(pg, total, &track, &thumb);
                pg->scroll_to += (in->py < thumb.y ? -1 : 1) * view_h() * 0.85f;
                break;
            }
            case HIT_CONTENT:
                if (pg->failed) { /* "try again" */
                    pg->failed = 0;
                    pg->status[0] = 0;
                }
                break;
            default: break;
            }
        } else {
            switch (b->focus) {
            case FOCUS_TOP:
                if (b->top_sel == TOP_LOGO)
                    return go_rail(b, RAIL_HOME);
                if (b->top_sel == TOP_QUALITY)
                    return cycle_quality(b);
                return ACT_OPEN_KEYBOARD;
            case FOCUS_RAIL: return go_rail(b, b->rail_sel);
            case FOCUS_CHIPS: return pick_chip(b, b->chip_sel);
            case FOCUS_GRID:
                if (pg->failed && last_row(nrows) < 0) {
                    pg->failed = 0;
                    pg->status[0] = 0;
                    break;
                }
                return play_selected(b, pg, nrows);
            }
        }
    }

    /* ---- D-pad ---- */
    switch (b->focus) {
    case FOCUS_TOP:
        if (k & UI_LEFT && b->top_sel > 0)
            b->top_sel--;
        if (k & UI_RIGHT && b->top_sel < TOP_QUALITY)
            b->top_sel++;
        if (k & UI_DOWN)
            b->focus = has_chips(b->page) ? FOCUS_CHIPS : FOCUS_GRID;
        break;
    case FOCUS_RAIL:
        if (k & UI_UP) {
            if (b->rail_sel > 0)
                b->rail_sel--;
            else {
                b->focus = FOCUS_TOP;
                b->top_sel = TOP_LOGO;
            }
        }
        if (k & UI_DOWN && b->rail_sel < RAIL_COUNT - 1)
            b->rail_sel++;
        if (k & UI_RIGHT)
            b->focus = FOCUS_GRID;
        break;
    case FOCUS_CHIPS:
        if (k & UI_LEFT) {
            if (b->chip_sel > 0)
                b->chip_sel--;
            else {
                b->focus = FOCUS_RAIL;
                b->rail_sel = RAIL_HOME;
            }
        }
        if (k & UI_RIGHT && b->chip_sel < chip_count(b) - 1)
            b->chip_sel++;
        if (k & UI_UP) {
            b->focus = FOCUS_TOP;
            b->top_sel = TOP_SEARCH;
        }
        if (k & UI_DOWN)
            b->focus = FOCUS_GRID;
        if (k & (UI_LEFT | UI_RIGHT))
            keep_chip_visible(b);
        break;
    case FOCUS_GRID:
        grid_keys(b, pg, k, nrows, total);
        break;
    }

    /* ---- scrolling: Nunchuk stick, then easing towards the targets ---- */
    if (in->stick_y > 0.2f || in->stick_y < -0.2f)
        pg->scroll_to += in->stick_y * 16;
    if (pg->scroll_to > ms)
        pg->scroll_to = ms;
    if (pg->scroll_to < 0)
        pg->scroll_to = 0;
    pg->scroll += (pg->scroll_to - pg->scroll) * 0.35f;
    if (pg->scroll_to - pg->scroll < 0.5f && pg->scroll_to - pg->scroll > -0.5f)
        pg->scroll = pg->scroll_to;
    for (i = 0; i < UI_SHELF_SLOTS; i++) {
        pg->shelf_scroll[i] += (pg->shelf_to[i] - pg->shelf_scroll[i]) * 0.35f;
        if (pg->shelf_to[i] - pg->shelf_scroll[i] < 0.01f && pg->shelf_to[i] - pg->shelf_scroll[i] > -0.01f)
            pg->shelf_scroll[i] = pg->shelf_to[i];
    }

    /* ---- what to load next ---- */
    if (!pg->failed && !pg->loading &&
        (!pg->started || (pg->feed.continuation && pg->scroll_to + view_h() * 2.5f >= total)))
        b->want_more = 1;
    return ACT_NONE;
}

/* ---- drawing ------------------------------------------------------------------------- */

static rgba_t avatar_color(const char *name)
{
    static const rgba_t colors[] = { 0xe53935ff, 0x8e24aaff, 0x3949abff, 0x039be5ff,
                                     0x00897bff, 0x7cb342ff, 0xfb8c00ff, 0x6d4c41ff };
    unsigned h = 5381;
    while (*name)
        h = h * 33 + (unsigned char)*name++;
    return colors[h % 8];
}

/* Draw img to fill the box, cropping whatever doesn't fit (like CSS object-fit: cover). */
static void draw_cover(const wt_image *img, float x, float y, float w, float h)
{
    const float ia = (float)img->w / img->h, ba = w / h;
    if (ia > ba * 1.02f) {
        const float keep = ba / ia;
        dr_image_part(img, x, y, w, h, (1 - keep) / 2, 0, (1 + keep) / 2, 1, 255);
    } else if (ia < ba / 1.02f) {
        const float keep = ia / ba;
        dr_image_part(img, x, y, w, h, 0, (1 - keep) / 2, 1, (1 + keep) / 2, 255);
    } else {
        dr_image(img, x, y, w, h, 255);
    }
}

static void draw_video_card(const yt_search_result *r, box c, int selected, ui_thumb_fn thumb, void *ud)
{
    const float th = thumb_h();
    const wt_image *img;
    char title[256], channel[128], meta[128], m[128], letter[2];
    float tx;

    if (c.y > UI_H || c.y + c.h < GRID_Y - 8)
        return;
    if (selected)
        ui_round_rect(c.x - 3, c.y - 3, c.w + 6, th + 6, THUMB_R + 3, C_FOCUS);
    img = thumb ? thumb(ud, r, 0) : NULL;
    if (img) {
        draw_cover(img, c.x, c.y, c.w, th);
        ui_round_corners(c.x, c.y, c.w, th, THUMB_R, selected ? C_FOCUS : C_BG);
    } else {
        ui_round_rect(c.x, c.y, c.w, th, THUMB_R, C_CHIP);
    }
    if (r->duration[0] && !r->is_short) {
        float dw = ui_text_width(FONT_META, r->duration) + 8;
        ui_round_rect(c.x + c.w - dw - 6, c.y + th - 22, dw, 17, 4, 0x000000cc);
        ui_text(FONT_META, c.x + c.w - dw - 2, c.y + th - 21, r->duration, C_TEXT, 0);
    } else if (r->is_short) {
        const float dw = ui_text_width(FONT_META, "SHORTS") + 22;
        ui_round_rect(c.x + c.w - dw - 6, c.y + th - 22, dw, 17, 4, 0x000000cc);
        ui_icon(ICON_SHORTS, c.x + c.w - dw - 4, c.y + th - 21, 14, 15, C_RED);
        ui_text(FONT_META, c.x + c.w - dw + 12, c.y + th - 21, "SHORTS", C_TEXT, 0);
    }
    ui_ascii(channel, sizeof channel, r->channel);
    letter[0] = channel[0] ? channel[0] : '?';
    letter[1] = 0;
    if (letter[0] >= 'a' && letter[0] <= 'z')
        letter[0] = (char)(letter[0] - 32);
    ui_circle(c.x + 14, c.y + th + 24, 14, avatar_color(channel));
    ui_text(FONT_TITLE, c.x + 14 - ui_text_width(FONT_TITLE, letter) / 2, c.y + th + 15, letter, C_TEXT, 0);
    tx = c.x + 36;
    ui_ascii(title, sizeof title, r->title);
    ui_text_wrap(FONT_TITLE, tx, c.y + th + 8, title, selected ? 0xffffffffu : C_TEXT, c.w - 38, 2);
    ui_text(FONT_META, tx, c.y + th + 10 + 2 * ui_line_height(FONT_TITLE), channel, C_TEXT2, c.w - 38);
    if (r->views[0] && r->published[0])
        snprintf(meta, sizeof meta, "%s \x7f %s", r->views, r->published);
    else
        snprintf(meta, sizeof meta, "%s%s", r->views, r->published);
    ui_ascii(m, sizeof m, meta);
    ui_text(FONT_META, tx, c.y + th + 10 + 2 * ui_line_height(FONT_TITLE) + ui_line_height(FONT_META), m, C_TEXT2,
            c.w - 38);
}

static void draw_short_card(const yt_search_result *r, box c, int selected, ui_thumb_fn thumb, void *ud)
{
    const float th = short_th();
    const wt_image *img;
    char title[256], views[64];

    if (c.y > UI_H || c.y + c.h < GRID_Y - 8 || c.x > ui_width() || c.x + c.w < 0)
        return;
    if (selected)
        ui_round_rect(c.x - 3, c.y - 3, c.w + 6, th + 6, THUMB_R + 3, C_FOCUS);
    img = thumb ? thumb(ud, r, 1) : NULL;
    if (img) {
        draw_cover(img, c.x, c.y, c.w, th);
        ui_round_corners(c.x, c.y, c.w, th, THUMB_R, selected ? C_FOCUS : C_BG);
    } else {
        ui_round_rect(c.x, c.y, c.w, th, THUMB_R, C_CHIP);
    }
    ui_ascii(title, sizeof title, r->title);
    ui_text_wrap(FONT_TITLE, c.x + 2, c.y + th + 8, title, selected ? 0xffffffffu : C_TEXT, c.w - 4, 2);
    ui_ascii(views, sizeof views, r->views);
    ui_text(FONT_META, c.x + 2, c.y + th + 8 + 2 * ui_line_height(FONT_TITLE), views, C_TEXT2, c.w - 4);
}

static void draw_arrow_button(box b, int right, int focused)
{
    const float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    ui_circle(cx, cy, 18, focused ? C_FOCUS : 0x272727ee);
    if (right)
        ui_icon(ICON_CHEVRON_RIGHT, cx - 10, cy - 10, 20, 20, focused ? 0x0f0f0fffu : C_TEXT);
    else {
        const atlas_rect *a = &atlas_icons[ICON_CHEVRON_RIGHT];
        dr_atlas(cx - 10, cy - 10, 20, 20, a->x + a->w, a->y, -a->w, a->h, focused ? 0x0f0f0fffu : C_TEXT);
    }
}

static void draw_rows(const browse_state *b, const browse_page *pg, int nrows, ui_thumb_fn thumb, void *ud,
                      const hit_t *hover)
{
    const int grid_focus = b->focus == FOCUS_GRID;
    int r, i;
    for (r = 0; r < nrows; r++) {
        const row_t *row = &g_rows[r];
        const float top = GRID_Y + row->y - pg->scroll;
        if (top > UI_H || top + row->h < GRID_Y - 8)
            continue;
        switch (row->type) {
        case ROW_VIDEOS:
            for (i = 0; i < row->count; i++)
                draw_video_card(&pg->feed.items[row->first + i], video_box(pg, row, i),
                                grid_focus && pg->row == r && pg->col == i, thumb, ud);
            break;
        case ROW_SHELF: {
            const float cx = content_x(), cw = content_w();
            const int first = (int)pg->shelf_scroll[slot(row->shelf)];
            ui_icon(ICON_SHORTS, cx, top + 4, 24, 24, C_RED);
            ui_text(FONT_HUGE, cx + 30, top + 2, "Shorts", C_TEXT, 0);
            dr_clip(cx - 4, GRID_Y - 6, cw + 8, UI_H - (GRID_Y - 6));
            for (i = first; i < row->count && i <= first + short_cols(); i++)
                draw_short_card(&pg->feed.items[row->first + i], shelf_box(pg, row, i),
                                grid_focus && pg->row == r && pg->col == i, thumb, ud);
            if (shelf_can_left(pg, row))
                draw_arrow_button(shelf_arrow(pg, row, 0), 0, hover->kind == HIT_SHELF_LEFT && hover->row == r);
            if (shelf_can_right(pg, row))
                draw_arrow_button(shelf_arrow(pg, row, 1), 1, hover->kind == HIT_SHELF_RIGHT && hover->row == r);
            dr_clip(0, GRID_Y - 6, ui_width(), UI_H - (GRID_Y - 6));
            break;
        }
        case ROW_FOOTER:
            if (pg->failed)
                ui_text(FONT_BODY, content_x() + (content_w() - ui_text_width(FONT_BODY, "Couldn't load more. Press A to try again.")) / 2,
                        top + 20, "Couldn't load more. Press A to try again.", C_TEXT2, 0);
            else
                ui_spinner(content_x() + content_w() / 2, top + 28, 14, b->frame);
            break;
        }
    }
}

static void draw_top_bar(const browse_state *b, const hit_t *hover)
{
    const box sb = search_box(), qb = quality_box(b), lb = logo_box(b);
    const int top = b->focus == FOCUS_TOP;
    const int sfocus = (top && b->top_sel == TOP_SEARCH) || hover->kind == HIT_SEARCH;
    const float btn = 56;
    const char *query = b->pages[PAGE_SEARCH].query;
    (void)hover;
    char label[32];

    if ((top && b->top_sel == TOP_LOGO) || hover->kind == HIT_LOGO)
        ui_round_rect(lb.x - 6, lb.y - 4, lb.w + 12, lb.h + 8, 8, 0xffffff1f);
    if (b->logo)
        dr_image(b->logo, lb.x + 2, lb.y, lb.w - 4, lb.h, 255);
    ui_round_rect(sb.x, sb.y, sb.w, sb.h, sb.h / 2, sfocus ? C_BLUE : C_BORDER);
    ui_round_rect(sb.x + 1, sb.y + 1, sb.w - 2, sb.h - 2, sb.h / 2 - 1, C_FIELD);
    ui_round_rect(sb.x + sb.w - btn, sb.y + 1, btn - 1, sb.h - 2, sb.h / 2 - 1, C_BUTTON);
    dr_fill(sb.x + sb.w - btn, sb.y + 1, btn / 2, sb.h - 2, C_BUTTON);
    dr_fill(sb.x + sb.w - btn, sb.y + 1, 1, sb.h - 2, sfocus ? C_BLUE : C_BORDER);
    ui_icon(ICON_SEARCH, sb.x + sb.w - btn / 2 - 10, sb.y + sb.h / 2 - 10, 20, 20, C_TEXT);
    if (b->page == PAGE_SEARCH && query[0]) {
        char q[64];
        ui_ascii(q, sizeof q, query);
        ui_text(FONT_BODY, sb.x + 18, sb.y + 8, q, C_TEXT, sb.w - btn - 28);
    } else {
        ui_text(FONT_BODY, sb.x + 18, sb.y + 8, "Search", C_TEXT2, 0);
    }
    if ((top && b->top_sel == TOP_QUALITY) || hover->kind == HIT_QUALITY)
        ui_round_rect(qb.x - 2, qb.y - 2, qb.w + 4, qb.h + 4, qb.h / 2 + 2, C_FOCUS);
    ui_round_rect(qb.x, qb.y, qb.w, qb.h, qb.h / 2, C_CHIP);
    snprintf(label, sizeof label, "%dp", b->quality);
    ui_text(FONT_META, qb.x + 12, qb.y + 7, "Quality ", C_TEXT2, 0);
    ui_text(FONT_TITLE, qb.x + 12 + ui_text_width(FONT_META, "Quality "), qb.y + 6, label, C_TEXT, 0);
}

static void draw_rail(const browse_state *b, const hit_t *hover)
{
    static const char *const labels[RAIL_COUNT] = { "Home", "Shorts", "Search" };
    static const int icons[RAIL_COUNT] = { ICON_HOME, ICON_SHORTS, ICON_SEARCH };
    int i;
    for (i = 0; i < RAIL_COUNT; i++) {
        const box r = rail_box(i);
        const int current = (i == RAIL_HOME && b->page == PAGE_HOME) || (i == RAIL_SEARCH && b->page == PAGE_SEARCH);
        const int focused = (b->focus == FOCUS_RAIL && b->rail_sel == i) || (hover->kind == HIT_RAIL && hover->index == i);
        const float lw = ui_text_width(FONT_META, labels[i]);
        if (focused)
            ui_round_rect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 12, C_FOCUS);
        if (current || focused)
            ui_round_rect(r.x, r.y, r.w, r.h, 10, current ? 0x303030ffu : C_BG);
        ui_icon(icons[i], r.x + (r.w - 26) / 2, r.y + 8, 26, 26, current ? C_TEXT : C_TEXT2);
        ui_text(FONT_META, r.x + (r.w - lw) / 2, r.y + 40, labels[i], current ? C_TEXT : C_TEXT2, 0);
    }
}

static void draw_chips(const browse_state *b, const browse_page *pg, const hit_t *hover)
{
    int i;
    dr_clip(content_x() - 4, CHIP_Y - 4, content_w() + 8, CHIP_H + 8);
    for (i = 0; i < chip_count(b); i++) {
        box c = chip_box(b, i);
        const int active = pg->chip == i;
        const int focused = (b->focus == FOCUS_CHIPS && b->chip_sel == i) || (hover->kind == HIT_CHIP && hover->index == i);
        if (c.x > ui_width() || c.x + c.w < 0)
            continue;
        if (focused)
            ui_round_rect(c.x - 2, c.y - 2, c.w + 4, c.h + 4, 10, C_FOCUS);
        ui_round_rect(c.x, c.y, c.w, c.h, 8, active ? C_CHIP_SEL : C_CHIP);
        ui_text(FONT_TITLE, c.x + 12, c.y + 6, chip_label(b, i), active ? 0x0f0f0fffu : C_TEXT, 0);
    }
    dr_clip(0, 0, 0, 0);
}

static void draw_scrollbar(const browse_state *b, const browse_page *pg, float total, const hit_t *hover)
{
    box track, thumb;
    const int hot = b->dragging || hover->kind == HIT_SB_THUMB || hover->kind == HIT_SB_TRACK;
    const float w = hot ? 12 : 6;
    if (max_scroll(total) <= 0)
        return;
    scrollbar(pg, total, &track, &thumb);
    ui_round_rect(track.x + (track.w - w) / 2, track.y, w, track.h, w / 2, hot ? 0xffffff20u : 0xffffff10u);
    ui_round_rect(thumb.x + (thumb.w - w) / 2, thumb.y, w, thumb.h, w / 2,
                  b->dragging ? C_TEXT : hot ? 0xccccccffu : 0x777777ffu);
}

void browse_draw(browse_state *b, ui_thumb_fn thumb, void *ud)
{
    browse_page *pg = &b->pages[b->page];
    float total;
    int nrows = layout(pg, &total);
    hit_t hover;
    memset(&hover, 0, sizeof hover);
    hover.kind = b->hover_kind;
    hover.index = b->hover_index;

    dr_fill(0, 0, ui_width(), UI_H, C_BG);
    if (last_row(nrows) < 0) {
        const float cx = content_x() + content_w() / 2;
        char m[300];
        if (pg->loading || (!pg->started && !pg->failed)) {
            ui_spinner(cx, 250, 18, b->frame);
        } else {
            const char *msg = pg->status[0] ? pg->status : "No videos here. Try another search.";
            ui_ascii(m, sizeof m, msg);
            ui_text_wrap_center(FONT_BODY, cx, 236, m, C_TEXT2, content_w() - 40, 3);
            if (pg->failed)
                ui_text(FONT_TITLE, cx - ui_text_width(FONT_TITLE, "Press A to try again") / 2, 300,
                        "Press A to try again", C_TEXT, 0);
        }
    } else {
        dr_clip(0, GRID_Y - 6, ui_width(), UI_H - (GRID_Y - 6));
        draw_rows(b, pg, nrows, thumb, ud, &hover);
        dr_clip(0, 0, 0, 0);
    }
    /* Fixed parts on top of the scrolling list. */
    dr_fill(0, 0, ui_width(), GRID_Y - 6, C_BG);
    dr_fill(0, 0, RAIL_W, UI_H, C_BG);
    draw_top_bar(b, &hover);
    draw_rail(b, &hover);
    if (has_chips(b->page))
        draw_chips(b, pg, &hover);
    else {
        char q[64], line[96];
        ui_ascii(q, sizeof q, pg->query);
        snprintf(line, sizeof line, "Results for \"%s\"", q);
        ui_text(FONT_BIG, content_x(), CHIP_Y + 4, line, C_TEXT, content_w());
    }
    draw_scrollbar(b, pg, total, &hover);
}
