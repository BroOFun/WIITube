/* On-screen QWERTY keyboard for searching. Big keys so the Wii Remote pointer hits them
 * easily; the D-pad moves between keys (up/down picks the key above/below). */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define KB_COLS 10
#define KB_LETTER_ROWS 4
static const char *const KB_ROWS[KB_LETTER_ROWS] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-.&" };

/* Bottom row: key label and width in key units. */
enum { K_SPACE, K_DELETE, K_CLEAR, K_SEARCH, K_SPECIAL };
static const char *const KB_SPECIAL[K_SPECIAL] = { "Space", "Delete", "Clear", "Search" };
static const int KB_SPECIAL_UNITS[K_SPECIAL] = { 4, 2, 2, 2 };
#define KB_ROWS_TOTAL (KB_LETTER_ROWS + 1)

typedef struct {
    float x, y, w, h;
} kbox;

typedef struct {
    kbox panel, field;
    float key, gap, kx, ky;
} kb_layout;

static kb_layout kb_geom(void)
{
    kb_layout l;
    l.panel.w = ui_width() - 48 > 600 ? 600 : ui_width() - 48;
    l.panel.h = 404;
    l.panel.x = (float)(int)((ui_width() - l.panel.w) / 2);
    l.panel.y = 38;
    l.field.x = l.panel.x + 20;
    l.field.y = l.panel.y + 52;
    l.field.w = l.panel.w - 40;
    l.field.h = 38;
    l.gap = 6;
    l.key = (float)(int)((l.panel.w - 40 - (KB_COLS - 1) * l.gap) / KB_COLS);
    l.kx = l.panel.x + (l.panel.w - (KB_COLS * l.key + (KB_COLS - 1) * l.gap)) / 2;
    l.ky = l.panel.y + 104;
    return l;
}

static int row_len(int row) { return row < KB_LETTER_ROWS ? KB_COLS : K_SPECIAL; }

static kbox kb_key(const kb_layout *l, int row, int col)
{
    kbox b;
    if (row < KB_LETTER_ROWS) {
        b.x = l->kx + col * (l->key + l->gap);
        b.y = l->ky + row * (l->key + l->gap);
        b.w = l->key;
        b.h = l->key;
    } else {
        int units = 0, i;
        for (i = 0; i < col; i++)
            units += KB_SPECIAL_UNITS[i];
        b.x = l->kx + units * (l->key + l->gap);
        b.y = l->ky + KB_LETTER_ROWS * (l->key + l->gap) + 4;
        b.w = KB_SPECIAL_UNITS[col] * (l->key + l->gap) - l->gap;
        b.h = 40;
    }
    return b;
}

void keyboard_open(keyboard_state *k, const char *text)
{
    memset(k, 0, sizeof *k);
    snprintf(k->text, sizeof k->text, "%s", text ? text : "");
    k->row = 1; /* on "q" */
    k->col = 0;
}

static void type_char(keyboard_state *k, char c)
{
    size_t len = strlen(k->text);
    if (len + 1 < sizeof k->text) {
        k->text[len] = c;
        k->text[len + 1] = 0;
    }
}

static int kb_press(keyboard_state *k, int row, int col)
{
    size_t len = strlen(k->text);
    if (row < KB_LETTER_ROWS) {
        type_char(k, KB_ROWS[row][col]);
    } else if (col == K_SPACE) {
        if (len > 0 && k->text[len - 1] != ' ')
            type_char(k, ' ');
    } else if (col == K_DELETE) {
        if (len > 0)
            k->text[len - 1] = 0;
    } else if (col == K_CLEAR) {
        k->text[0] = 0;
    } else if (len > 0) {
        return ACT_KB_SEARCH;
    }
    return ACT_NONE;
}

/* The key in row `to` whose centre is closest to the centre of the current key. */
static int nearest_col(const kb_layout *l, int from_row, int col, int to)
{
    const kbox a = kb_key(l, from_row, col);
    const float cx = a.x + a.w / 2;
    int best = 0, i;
    float bestd = 1e9f;
    for (i = 0; i < row_len(to); i++) {
        const kbox b = kb_key(l, to, i);
        float d = b.x + b.w / 2 - cx;
        if (d < 0)
            d = -d;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

static int inside(const kbox *b, float x, float y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

int keyboard_update(keyboard_state *k, const ui_input *in)
{
    const kb_layout l = kb_geom();
    const unsigned p = in->pressed;
    int hover_row = -1, hover_col = -1, r, c;

    k->frame++;
    if (in->pointer) {
        for (r = 0; r < KB_ROWS_TOTAL; r++)
            for (c = 0; c < row_len(r); c++) {
                kbox b = kb_key(&l, r, c);
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
    if (p & UI_1)
        type_char(k, ' ');
    if (p & UI_A) {
        if (in->pointer && hover_row < 0) {
            /* Clicking outside the panel closes the keyboard. */
            kbox panel = l.panel;
            return inside(&panel, in->px, in->py) ? ACT_NONE : ACT_KB_CANCEL;
        }
        if (in->pointer) {
            k->row = hover_row;
            k->col = hover_col;
        }
        return kb_press(k, k->row, k->col);
    }
    if (p & UI_LEFT)
        k->col = (k->col + row_len(k->row) - 1) % row_len(k->row);
    if (p & UI_RIGHT)
        k->col = (k->col + 1) % row_len(k->row);
    if (p & (UI_UP | UI_DOWN)) {
        const int to = (k->row + ((p & UI_UP) ? KB_ROWS_TOTAL - 1 : 1)) % KB_ROWS_TOTAL;
        k->col = nearest_col(&l, k->row, k->col, to);
        k->row = to;
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
    ui_text(FONT_BIG, l.panel.x + 20, l.panel.y + 18, "Search YouTube", C_TEXT, 0);
    ui_round_rect(l.field.x, l.field.y, l.field.w, l.field.h, l.field.h / 2, C_BLUE);
    ui_round_rect(l.field.x + 2, l.field.y + 2, l.field.w - 4, l.field.h - 4, l.field.h / 2 - 2, C_FIELD);
    ui_icon(ICON_SEARCH, l.field.x + 14, l.field.y + 10, 18, 18, C_TEXT2);
    snprintf(shown, sizeof shown, "%s%s", k->text, (k->frame / 30) % 2 ? "" : "|");
    ui_text(FONT_BODY, l.field.x + 40, l.field.y + 10, shown, C_TEXT, l.field.w - 56);

    for (r = 0; r < KB_ROWS_TOTAL; r++)
        for (c = 0; c < row_len(r); c++) {
            const kbox b = kb_key(&l, r, c);
            const int sel = r == k->row && c == k->col;
            const int font = r < KB_LETTER_ROWS ? FONT_HUGE : FONT_TITLE;
            const char *label;
            char ch[2];
            rgba_t bg = sel ? C_CHIP_SEL : C_KEY, fg = sel ? 0x0f0f0fffu : C_TEXT;
            if (r == KB_LETTER_ROWS && c == K_SEARCH && !sel) {
                bg = C_BLUE;
                fg = 0x0f0f0fffu;
            }
            ui_round_rect(b.x, b.y, b.w, b.h, 7, bg);
            if (r < KB_LETTER_ROWS) {
                ch[0] = KB_ROWS[r][c];
                ch[1] = 0;
                label = ch;
            } else {
                label = KB_SPECIAL[c];
            }
            ui_text(font, b.x + (b.w - ui_text_width(font, label)) / 2, b.y + (b.h - ui_line_height(font)) / 2 + 2,
                    label, fg, 0);
        }
    ui_text(FONT_META, l.panel.x + 20, l.panel.y + l.panel.h - 24,
            "A: type    B: delete    1: space    +: search    -: cancel", C_TEXT2, 0);
}
