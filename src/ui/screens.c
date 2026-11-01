/* YouTube-style screens: browse grid, search keyboard, player controls, messages. */
#include "ui.h"

#include <stdio.h>
#include <string.h>

const char *const ui_chips[UI_NUM_CHIPS] = {
    "Gaming", "Wii", "Super Mario", "Mario Kart", "Music", "Minecraft", "Game design", "Podcasts",
};

/* ---- layout ------------------------------------------------------------------------ */

#define TOP_H     52
#define CHIP_Y    60
#define CHIP_H    30
#define GRID_Y    104
#define MARGIN    16
#define GAP_X     14
#define GAP_Y     18
#define THUMB_R   8

typedef struct {
    float x, y, w, h;
} box;

static int inside(const box *b, float x, float y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static int grid_cols(void) { return ui_width() >= 800 ? 4 : 3; }

static float card_w(void)
{
    int cols = grid_cols();
    return (float)(int)((ui_width() - 2 * MARGIN - (cols - 1) * GAP_X) / cols);
}

static float thumb_h(void) { return (float)(int)(card_w() * 9 / 16); }

static float card_h(void)
{
    return thumb_h() + 10 + 2 * ui_line_height(FONT_TITLE) + 2 + 2 * ui_line_height(FONT_META);
}

static box search_box(void)
{
    float w = ui_width() * 0.46f;
    box b;
    if (w > 440)
        w = 440;
    b.x = (float)(int)((ui_width() - w) / 2);
    b.y = 9;
    b.w = w;
    b.h = 34;
    return b;
}

static box quality_box(const browse_state *bs)
{
    char label[32];
    box b;
    snprintf(label, sizeof label, "%dp", bs->quality);
    b.w = ui_text_width(FONT_TITLE, label) + ui_text_width(FONT_META, "Quality ") + 24;
    b.h = 28;
    b.x = ui_width() - MARGIN - b.w;
    b.y = 12;
    return b;
}

static float chip_w(int i) { return ui_text_width(FONT_TITLE, ui_chips[i]) + 24; }

static box chip_box(const browse_state *bs, int i)
{
    box b;
    int k;
    b.x = MARGIN - bs->chip_scroll;
    for (k = 0; k < i; k++)
        b.x += chip_w(k) + 10;
    b.y = CHIP_Y;
    b.w = chip_w(i);
    b.h = CHIP_H;
    return b;
}

static box card_box(const browse_state *bs, int i)
{
    const int cols = grid_cols();
    box b;
    b.w = card_w();
    b.h = card_h();
    b.x = MARGIN + (i % cols) * (b.w + GAP_X);
    b.y = GRID_Y + (i / cols) * (b.h + GAP_Y) - bs->scroll;
    return b;
}

/* ---- browse --------------------------------------------------------------------------- */

void browse_init(browse_state *b, const wt_image *logo)
{
    memset(b, 0, sizeof *b);
    b->logo = logo;
    b->quality = 480;
    b->focus = FOCUS_GRID;
}

static float target_scroll(const browse_state *b)
{
    const int cols = grid_cols();
    const float row_h = card_h() + GAP_Y;
    const int row = b->sel / cols;
    float top = row * row_h, s = b->scroll;
    const float view = UI_H - GRID_Y - 8;
    if (b->focus != FOCUS_GRID)
        return 0;
    if (top - s < 0)
        s = top;
    if (top + card_h() - s > view)
        s = top + card_h() - view;
    return s < 0 ? 0 : s;
}

static void keep_chip_visible(browse_state *b)
{
    box c = chip_box(b, b->chip_sel);
    if (c.x < MARGIN)
        b->chip_scroll -= MARGIN - c.x;
    if (c.x + c.w > ui_width() - MARGIN)
        b->chip_scroll += c.x + c.w - (ui_width() - MARGIN);
}

int browse_update(browse_state *b, const ui_input *in)
{
    const int cols = grid_cols();
    const unsigned k = in->pressed;
    int i, hovered = -1; /* -1 none, -2 search, -3 quality, 100+i chip, i card */

    b->frame++;
    if (in->pointer) {
        box sb = search_box(), qb = quality_box(b);
        if (inside(&sb, in->px, in->py))
            hovered = -2;
        else if (inside(&qb, in->px, in->py))
            hovered = -3;
        else if (in->py >= CHIP_Y && in->py < CHIP_Y + CHIP_H) {
            for (i = 0; i < UI_NUM_CHIPS; i++) {
                box c = chip_box(b, i);
                if (inside(&c, in->px, in->py))
                    hovered = 100 + i;
            }
        } else if (in->py >= GRID_Y - 4) {
            for (i = 0; i < b->nresults; i++) {
                box c = card_box(b, i);
                if (inside(&c, in->px, in->py))
                    hovered = i;
            }
        }
        if (in->pointer_moved) {
            if (hovered == -2)
                b->focus = FOCUS_SEARCH;
            else if (hovered >= 100) {
                b->focus = FOCUS_CHIPS;
                b->chip_sel = hovered - 100;
            } else if (hovered >= 0) {
                b->focus = FOCUS_GRID;
                b->sel = hovered;
            }
        }
    }

    if (k & UI_HOME)
        return ACT_QUIT;
    if (k & UI_PLUS)
        return ACT_OPEN_KEYBOARD;
    if (k & UI_1) {
        b->quality = b->quality == 144 ? 240 : b->quality == 240 ? 360 : b->quality == 360 ? 480 : 144;
        return ACT_QUALITY;
    }
    if (k & UI_A) {
        if (in->pointer && hovered == -3) {
            b->quality = b->quality == 144 ? 240 : b->quality == 240 ? 360 : b->quality == 360 ? 480 : 144;
            return ACT_QUALITY;
        }
        if (in->pointer && hovered == -1)
            return ACT_NONE; /* clicked on nothing */
        if (b->focus == FOCUS_SEARCH)
            return ACT_OPEN_KEYBOARD;
        if (b->focus == FOCUS_CHIPS) {
            b->chip = b->chip_sel;
            return ACT_CHIP;
        }
        if (b->focus == FOCUS_GRID && b->nresults > 0 && !b->loading)
            return ACT_PLAY;
    }

    switch (b->focus) {
    case FOCUS_SEARCH:
        if (k & UI_DOWN)
            b->focus = FOCUS_CHIPS;
        break;
    case FOCUS_CHIPS:
        if (k & UI_LEFT && b->chip_sel > 0)
            b->chip_sel--;
        if (k & UI_RIGHT && b->chip_sel < UI_NUM_CHIPS - 1)
            b->chip_sel++;
        if (k & UI_UP)
            b->focus = FOCUS_SEARCH;
        if (k & UI_DOWN && b->nresults > 0)
            b->focus = FOCUS_GRID;
        if (k & (UI_LEFT | UI_RIGHT))
            keep_chip_visible(b);
        break;
    case FOCUS_GRID:
        if (b->nresults == 0) {
            if (k & UI_UP)
                b->focus = FOCUS_CHIPS;
            break;
        }
        if (k & UI_LEFT && b->sel % cols > 0)
            b->sel--;
        if (k & UI_RIGHT && b->sel % cols < cols - 1 && b->sel + 1 < b->nresults)
            b->sel++;
        if (k & UI_DOWN && b->sel + cols < b->nresults)
            b->sel += cols;
        else if (k & UI_DOWN && b->sel / cols < (b->nresults - 1) / cols)
            b->sel = b->nresults - 1;
        if (k & UI_UP) {
            if (b->sel < cols)
                b->focus = FOCUS_CHIPS;
            else
                b->sel -= cols;
        }
        break;
    }
    /* Smooth scrolling towards the selected row (D-pad only; pointing doesn't scroll). */
    if (!(in->pointer && !(k & UI_DIRS)))
        b->scroll += (target_scroll(b) - b->scroll) * 0.35f;
    else if (b->focus != FOCUS_GRID)
        b->scroll += (0 - b->scroll) * 0.35f;
    return ACT_NONE;
}

static rgba_t avatar_color(const char *name)
{
    static const rgba_t colors[] = { 0xe53935ff, 0x8e24aaff, 0x3949abff, 0x039be5ff,
                                     0x00897bff, 0x7cb342ff, 0xfb8c00ff, 0x6d4c41ff };
    unsigned h = 5381;
    while (*name)
        h = h * 33 + (unsigned char)*name++;
    return colors[h % 8];
}

static void draw_card(const browse_state *b, int i, ui_thumb_fn thumb, void *ud)
{
    const yt_search_result *r = &b->results[i];
    const box c = card_box(b, i);
    const float th = thumb_h();
    const int selected = b->focus == FOCUS_GRID && b->sel == i;
    const wt_image *img;
    char title[256], channel[128], meta[128], letter[2];
    float tx;

    if (c.y > UI_H || c.y + c.h < GRID_Y - 8)
        return;
    if (selected)
        ui_round_rect(c.x - 3, c.y - 3, c.w + 6, th + 6, THUMB_R + 3, C_FOCUS);
    img = thumb ? thumb(ud, r->video_id) : NULL;
    if (img) {
        dr_image(img, c.x, c.y, c.w, th, 255);
        ui_round_corners(c.x, c.y, c.w, th, THUMB_R, selected ? C_FOCUS : C_BG);
    } else {
        ui_round_rect(c.x, c.y, c.w, th, THUMB_R, C_CHIP);
    }
    if (r->duration[0]) {
        float dw = ui_text_width(FONT_META, r->duration) + 8;
        ui_round_rect(c.x + c.w - dw - 6, c.y + th - 22, dw, 17, 4, 0x000000cc);
        ui_text(FONT_META, c.x + c.w - dw - 2, c.y + th - 21, r->duration, C_TEXT, 0);
    }

    /* Channel "avatar": a coloured circle with the first letter. */
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
    {
        char m[128];
        ui_ascii(m, sizeof m, meta);
        ui_text(FONT_META, tx, c.y + th + 10 + 2 * ui_line_height(FONT_TITLE) + ui_line_height(FONT_META), m,
                C_TEXT2, c.w - 38);
    }
}

static void draw_top_bar(const browse_state *b)
{
    const box sb = search_box(), qb = quality_box(b);
    const int focused = b->focus == FOCUS_SEARCH;
    const float btn = 56;
    char label[32];

    if (b->logo) {
        float h = 28, w = b->logo->w * h / b->logo->h;
        dr_image(b->logo, MARGIN, 12, w, h, 255);
    }
    /* Search field (pill) + magnifier button, like YouTube's. */
    ui_round_rect(sb.x, sb.y, sb.w, sb.h, sb.h / 2, focused ? C_BLUE : C_BORDER);
    ui_round_rect(sb.x + 1, sb.y + 1, sb.w - 2, sb.h - 2, sb.h / 2 - 1, C_FIELD);
    /* Button: square left edge, rounded right end, 1px divider. */
    ui_round_rect(sb.x + sb.w - btn, sb.y + 1, btn - 1, sb.h - 2, sb.h / 2 - 1, C_BUTTON);
    dr_fill(sb.x + sb.w - btn, sb.y + 1, btn / 2, sb.h - 2, C_BUTTON);
    dr_fill(sb.x + sb.w - btn, sb.y + 1, 1, sb.h - 2, focused ? C_BLUE : C_BORDER);
    ui_icon(ICON_SEARCH, sb.x + sb.w - btn / 2 - 10, sb.y + sb.h / 2 - 10, 20, 20, C_TEXT);
    if (b->query[0] && b->chip < 0) {
        char q[64];
        ui_ascii(q, sizeof q, b->query);
        ui_text(FONT_BODY, sb.x + 18, sb.y + 8, q, C_TEXT, sb.w - btn - 28);
    } else {
        ui_text(FONT_BODY, sb.x + 18, sb.y + 8, "Search", C_TEXT2, 0);
    }
    /* Quality pill on the right. */
    ui_round_rect(qb.x, qb.y, qb.w, qb.h, qb.h / 2, C_CHIP);
    snprintf(label, sizeof label, "%dp", b->quality);
    ui_text(FONT_META, qb.x + 12, qb.y + 7, "Quality ", C_TEXT2, 0);
    ui_text(FONT_TITLE, qb.x + 12 + ui_text_width(FONT_META, "Quality "), qb.y + 6, label, C_TEXT, 0);
}

static void draw_chips(const browse_state *b)
{
    int i;
    dr_clip(0, CHIP_Y - 4, ui_width(), CHIP_H + 8);
    for (i = 0; i < UI_NUM_CHIPS; i++) {
        box c = chip_box(b, i);
        const int active = b->chip == i;
        const int focused = b->focus == FOCUS_CHIPS && b->chip_sel == i;
        if (c.x > ui_width() || c.x + c.w < 0)
            continue;
        if (focused)
            ui_round_rect(c.x - 2, c.y - 2, c.w + 4, c.h + 4, 10, C_FOCUS);
        ui_round_rect(c.x, c.y, c.w, c.h, 8, active ? C_CHIP_SEL : C_CHIP);
        ui_text(FONT_TITLE, c.x + 12, c.y + 6, ui_chips[i], active ? 0x0f0f0fffu : C_TEXT, 0);
    }
    dr_clip(0, 0, 0, 0);
}

void browse_draw(browse_state *b, ui_thumb_fn thumb, void *ud)
{
    int i;
    dr_fill(0, 0, ui_width(), UI_H, C_BG);
    if (b->status || b->nresults == 0) {
        const char *msg = b->status ? b->status : "No videos here. Try another search.";
        char m[256];
        ui_ascii(m, sizeof m, msg);
        if (b->loading)
            ui_spinner(ui_width() / 2.0f, 250, 18, b->frame);
        ui_text(FONT_BODY, (ui_width() - ui_text_width(FONT_BODY, m)) / 2, b->loading ? 286 : 250, m,
                C_TEXT2, ui_width() - 40);
    } else {
        dr_clip(0, GRID_Y - 6, ui_width(), UI_H - (GRID_Y - 6));
        for (i = 0; i < b->nresults; i++)
            draw_card(b, i, thumb, ud);
        dr_clip(0, 0, 0, 0);
    }
    /* Top bar and chips stay on top of the scrolling grid. */
    dr_fill(0, 0, ui_width(), GRID_Y - 6, C_BG);
    draw_top_bar(b);
    draw_chips(b);
}

/* ---- keyboard ----------------------------------------------------------------------------- */

static const char *const KB_ROWS[3] = { "abcdefghijklm", "nopqrstuvwxyz", "0123456789-'." };
#define KB_COLS 13
static const char *const KB_SPECIAL[4] = { "Space", "Delete", "Clear", "Search" };

typedef struct {
    box panel, field;
    float key, gap, kx, ky;
} kb_layout;

static kb_layout kb_geom(void)
{
    kb_layout l;
    l.panel.w = ui_width() - 64 > 600 ? 600 : ui_width() - 64;
    l.panel.h = 300;
    l.panel.x = (ui_width() - l.panel.w) / 2;
    l.panel.y = 96;
    l.field.x = l.panel.x + 20;
    l.field.y = l.panel.y + 50;
    l.field.w = l.panel.w - 40;
    l.field.h = 38;
    l.gap = 6;
    l.key = (float)(int)((l.panel.w - 40 - (KB_COLS - 1) * l.gap) / KB_COLS);
    l.kx = l.panel.x + (l.panel.w - (KB_COLS * l.key + (KB_COLS - 1) * l.gap)) / 2;
    l.ky = l.panel.y + 102;
    return l;
}

static box kb_key(const kb_layout *l, int row, int col)
{
    box b;
    if (row < 3) {
        b.x = l->kx + col * (l->key + l->gap);
        b.y = l->ky + row * (l->key + l->gap);
        b.w = l->key;
        b.h = l->key;
    } else {
        const float total = KB_COLS * l->key + (KB_COLS - 1) * l->gap;
        const float w = (total - 3 * l->gap) / 4;
        b.x = l->kx + col * (w + l->gap);
        b.y = l->ky + 3 * (l->key + l->gap) + 4;
        b.w = w;
        b.h = 36;
    }
    return b;
}

void keyboard_open(keyboard_state *k, const char *text)
{
    memset(k, 0, sizeof *k);
    snprintf(k->text, sizeof k->text, "%s", text ? text : "");
    k->row = 0;
    k->col = 0;
}

static int kb_press(keyboard_state *k, int row, int col)
{
    size_t len = strlen(k->text);
    if (row < 3) {
        if (len + 1 < sizeof k->text) {
            k->text[len] = KB_ROWS[row][col];
            k->text[len + 1] = 0;
        }
    } else if (col == 0) {
        if (len > 0 && len + 1 < sizeof k->text && k->text[len - 1] != ' ') {
            k->text[len] = ' ';
            k->text[len + 1] = 0;
        }
    } else if (col == 1) {
        if (len > 0)
            k->text[len - 1] = 0;
    } else if (col == 2) {
        k->text[0] = 0;
    } else if (len > 0) {
        return ACT_KB_SEARCH;
    }
    return ACT_NONE;
}

int keyboard_update(keyboard_state *k, const ui_input *in)
{
    const kb_layout l = kb_geom();
    const unsigned p = in->pressed;
    int hover_row = -1, hover_col = -1, r, c;

    k->frame++;
    if (in->pointer) {
        for (r = 0; r < 4; r++)
            for (c = 0; c < (r < 3 ? KB_COLS : 4); c++) {
                box b = kb_key(&l, r, c);
                if (inside(&b, in->px, in->py)) {
                    hover_row = r;
                    hover_col = c;
                }
            }
        if (in->pointer_moved && hover_row >= 0) {
            k->row = hover_row;
            k->col = hover_col;
        }
    }
    if (p & (UI_HOME | UI_MINUS))
        return ACT_KB_CANCEL;
    if (p & UI_PLUS)
        return strlen(k->text) ? ACT_KB_SEARCH : ACT_NONE;
    if (p & UI_B) {
        size_t len = strlen(k->text);
        if (len == 0)
            return ACT_KB_CANCEL;
        k->text[len - 1] = 0;
    }
    if (p & UI_A) {
        if (in->pointer && hover_row < 0)
            return ACT_NONE;
        return kb_press(k, k->row, k->col);
    }
    if (p & UI_LEFT)
        k->col = (k->col + (k->row < 3 ? KB_COLS : 4) - 1) % (k->row < 3 ? KB_COLS : 4);
    if (p & UI_RIGHT)
        k->col = (k->col + 1) % (k->row < 3 ? KB_COLS : 4);
    if (p & (UI_UP | UI_DOWN)) {
        int from = k->row;
        k->row = (k->row + ((p & UI_UP) ? 3 : 1)) % 4;
        if (from == 3 && k->row != 3)
            k->col = k->col * KB_COLS / 4 + 1;
        else if (from != 3 && k->row == 3)
            k->col = k->col * 4 / KB_COLS;
    }
    return ACT_NONE;
}

void keyboard_draw(keyboard_state *k)
{
    const kb_layout l = kb_geom();
    char shown[80];
    int r, c;

    dr_fill(0, 0, ui_width(), UI_H, 0x000000b8);
    ui_round_rect(l.panel.x, l.panel.y, l.panel.w, l.panel.h, 14, C_PANEL);
    ui_text(FONT_BIG, l.panel.x + 20, l.panel.y + 16, "Search YouTube", C_TEXT, 0);
    ui_round_rect(l.field.x, l.field.y, l.field.w, l.field.h, l.field.h / 2, C_BLUE);
    ui_round_rect(l.field.x + 2, l.field.y + 2, l.field.w - 4, l.field.h - 4, l.field.h / 2 - 2, C_FIELD);
    ui_icon(ICON_SEARCH, l.field.x + 14, l.field.y + 10, 18, 18, C_TEXT2);
    snprintf(shown, sizeof shown, "%s%s", k->text, (k->frame / 30) % 2 ? "" : "|");
    ui_text(FONT_BODY, l.field.x + 40, l.field.y + 10, shown, C_TEXT, l.field.w - 56);

    for (r = 0; r < 4; r++)
        for (c = 0; c < (r < 3 ? KB_COLS : 4); c++) {
            const box b = kb_key(&l, r, c);
            const int sel = r == k->row && c == k->col;
            const char *label;
            char ch[2];
            rgba_t bg = sel ? C_CHIP_SEL : C_KEY, fg = sel ? 0x0f0f0fffu : C_TEXT;
            if (r == 3 && c == 3 && !sel) {
                bg = C_BLUE;
                fg = 0x0f0f0fffu;
            }
            ui_round_rect(b.x, b.y, b.w, b.h, 6, bg);
            if (r < 3) {
                ch[0] = KB_ROWS[r][c];
                ch[1] = 0;
                label = ch;
            } else {
                label = KB_SPECIAL[c];
            }
            ui_text(r < 3 ? FONT_BIG : FONT_TITLE, b.x + (b.w - ui_text_width(r < 3 ? FONT_BIG : FONT_TITLE, label)) / 2,
                    b.y + (b.h - ui_line_height(r < 3 ? FONT_BIG : FONT_TITLE)) / 2 + 1, label, fg, 0);
        }
    ui_text(FONT_META, l.panel.x + 20, l.panel.y + l.panel.h - 24,
            "A: type    B: delete    +: search    -: cancel", C_TEXT2, 0);
}

/* ---- player ---------------------------------------------------------------------------- */

static void fmt_time(double s, char *out, size_t n)
{
    long t = s > 0 ? (long)s : 0;
    if (t >= 3600)
        snprintf(out, n, "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    else
        snprintf(out, n, "%ld:%02ld", t / 60, t % 60);
}

void ui_fit(int w, int h, float *x, float *y, float *dw, float *dh)
{
    const float W = (float)ui_width(), H = UI_H;
    float va = (float)w / (float)h;
    *dw = W;
    *dh = H;
    if (va > W / H)
        *dh = W / va;
    else
        *dw = H * va;
    *x = (W - *dw) / 2;
    *y = (H - *dh) / 2;
}

int player_update(player_view *p, const ui_input *in)
{
    const unsigned k = in->pressed;
    p->frame++;
    if (p->overlay_timer > 0)
        p->overlay_timer--;
    if (p->volume_timer > 0)
        p->volume_timer--;
    if (k || in->pointer_moved)
        p->overlay_timer = 180;
    if (k & (UI_B | UI_HOME))
        return ACT_PLAYER_BACK;
    if (k & (UI_A | UI_2))
        return ACT_PLAYER_PAUSE;
    if (k & (UI_PLUS | UI_MINUS)) {
        p->volume += (k & UI_PLUS) ? 32 : -32;
        p->volume = p->volume < 0 ? 0 : p->volume > 255 ? 255 : p->volume;
        p->volume_timer = 120;
        return ACT_PLAYER_VOLUME;
    }
    return ACT_NONE;
}

void player_draw_overlay(player_view *p)
{
    const float W = (float)ui_width(), H = UI_H;
    const int show = p->overlay_timer > 0 || p->paused;
    char t1[16], t2[16], times[40], title[256], channel[128], q[16];

    if (p->buffering && !p->paused) {
        ui_circle(W / 2, H / 2, 34, 0x00000088);
        ui_spinner(W / 2, H / 2, 20, p->frame);
    }
    if (!show)
        return;

    /* Top: title over a fade. */
    dr_vgradient(0, 0, W, 96, 0x000000c8, 0x00000000);
    ui_ascii(title, sizeof title, p->title);
    ui_ascii(channel, sizeof channel, p->channel);
    ui_text(FONT_BIG, 24, 18, title, C_TEXT, W - 48);
    ui_text(FONT_SMALL, 24, 44, channel, C_TEXT2, W - 48);

    /* Bottom: progress bar and controls over a fade. */
    {
        const float bx = 24, bw = W - 48, by = H - 58;
        float played = p->duration > 0 ? (float)(p->position / p->duration) : 0;
        if (played < 0)
            played = 0;
        if (played > 1)
            played = 1;
        dr_vgradient(0, H - 120, W, 120, 0x00000000, 0x000000d8);
        dr_fill(bx, by, bw, 4, 0xffffff4c);
        dr_fill(bx, by, bw * (p->buffered > 1 ? 1 : p->buffered), 4, 0xffffff80);
        dr_fill(bx, by, bw * played, 4, C_RED);
        ui_circle(bx + bw * played, by + 2, 7, C_RED);
    }
    ui_icon(p->paused ? ICON_PLAY : ICON_PAUSE, 22, H - 44, 26, 26, C_TEXT);
    ui_icon(ICON_VOLUME, 62, H - 42, 22, 22, C_TEXT);
    dr_fill(88, H - 32, 52, 3, 0xffffff4c);
    dr_fill(88, H - 32, 52.0f * p->volume / 255, 3, C_TEXT);
    ui_circle(88 + 52.0f * p->volume / 255, H - 30.5f, 5, C_TEXT);
    fmt_time(p->position, t1, sizeof t1);
    fmt_time(p->duration, t2, sizeof t2);
    snprintf(times, sizeof times, "%s / %s", t1, t2);
    ui_text(FONT_SMALL, 156, H - 40, times, C_TEXT, 0);
    snprintf(q, sizeof q, "%dp", p->quality);
    {
        float qw = ui_text_width(FONT_TITLE, q) + 16;
        const char *hint = "B: back    +/-: volume";
        ui_round_rect(W - 24 - qw, H - 42, qw, 22, 4, 0xffffff26);
        ui_text(FONT_TITLE, W - 24 - qw + 8, H - 40, q, C_TEXT, 0);
        ui_text(FONT_META, W - 36 - qw - ui_text_width(FONT_META, hint), H - 38, hint, C_TEXT2, 0);
    }

    if (p->paused) {
        ui_circle(W / 2, H / 2, 40, 0x00000099);
        ui_icon(ICON_PLAY, W / 2 - 20, H / 2 - 22, 44, 44, C_TEXT);
    }
}

/* ---- messages ------------------------------------------------------------------------------ */

void ui_message(const wt_image *logo, const char *title, const char *detail, int busy, unsigned frame)
{
    const float W = (float)ui_width();
    char t[256], d[512];
    dr_fill(0, 0, W, UI_H, C_BG);
    if (logo) {
        float h = 72, w = logo->w * h / logo->h;
        dr_image(logo, (W - w) / 2, 110, w, h, 255);
    }
    ui_ascii(t, sizeof t, title ? title : "");
    if (busy) {
        ui_spinner(W / 2, 262, 18, frame);
        ui_text(FONT_BODY, (W - ui_text_width(FONT_BODY, t)) / 2, 300, t, C_TEXT2, W - 40);
        return;
    }
    ui_text(FONT_HUGE, (W - ui_text_width(FONT_HUGE, t)) / 2, 230, t, C_TEXT, W - 40);
    if (detail) {
        const float maxw = W - 120;
        ui_ascii(d, sizeof d, detail);
        ui_text_wrap_center(FONT_BODY, W / 2, 276, d, C_TEXT2, maxw, 4);
    }
    {
        const char *hint = "Press A to continue";
        float hw = ui_text_width(FONT_TITLE, hint) + 32;
        ui_round_rect((W - hw) / 2, 400, hw, 34, 17, C_CHIP);
        ui_text(FONT_TITLE, (W - hw) / 2 + 16, 408, hint, C_TEXT, 0);
    }
}
