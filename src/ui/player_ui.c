/* Player controls drawn over the video: progress bar (click to jump, D-pad / Nunchuk to skip
 * 10 s), volume, captions, and a settings menu (quality, captions, audio track). Shorts get
 * a vertical layout with previous / next buttons. */
#include "ui.h"

#include <stdio.h>
#include <string.h>

#define NUDGE_SECONDS 10
#define NUDGE_WAIT 36       /* frames after the last press before jumping */
#define MENU_W 290
#define MENU_ROW 34

void player_view_init(player_view *p)
{
    memset(p, 0, sizeof *p);
    p->volume = 255;
    p->overlay_timer = 240;
    p->nudge_target = -1;
    p->hover_bar = -1;
}

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

void player_video_rect(const player_view *p, int w, int h, float *x, float *y, float *dw, float *dh)
{
    ui_fit(w, h, x, y, dw, dh); /* vertical video is fitted to the screen height either way */
    (void)p;
}

/* ---- geometry ---- */

typedef struct {
    float x, y, w, h;
} pbox;

static pbox mk(float x, float y, float w, float h)
{
    pbox b;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    return b;
}

static int in_box(pbox b, float x, float y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

/* Shorts: the column the vertical video sits in. */
static pbox short_column(void)
{
    const float w = (float)(int)(UI_H * 9 / 16);
    return mk((ui_width() - w) / 2, 0, w, UI_H);
}

static pbox bar_box(const player_view *p)
{
    if (p->is_short) {
        pbox c = short_column();
        return mk(c.x, UI_H - 4, c.w, 4);
    }
    return mk(24, UI_H - 58, ui_width() - 48, 4);
}

static pbox back_box(void) { return mk(10, 12, 36, 36); }
static pbox volume_box(void) { return mk(86, UI_H - 42, 58, 22); }

static pbox gear_box(const player_view *p)
{
    if (p->is_short) {
        pbox c = short_column();
        return mk(c.x + c.w + 22, UI_H - 70, 44, 44);
    }
    return mk(ui_width() - 24 - 30, UI_H - 46, 30, 30);
}

static pbox cc_box(const player_view *p)
{
    if (p->is_short) {
        pbox g = gear_box(p);
        return mk(g.x, g.y - 56, 44, 44);
    }
    {
        pbox g = gear_box(p);
        return mk(g.x - 46, g.y + 3, 38, 24);
    }
}

static pbox updown_box(int down)
{
    pbox c = short_column();
    return mk(c.x + c.w + 22, UI_H / 2 - 54 + (down ? 64 : 0), 44, 44);
}

/* ---- settings menu ---- */

static int menu_rows(const player_view *p)
{
    switch (p->menu) {
    case MENU_MAIN: return p->naudio > 1 ? 3 : 2;
    case MENU_QUALITY: return 1 + p->nqualities;
    case MENU_CAPTIONS: return 1 + (p->cc_available ? 2 : 1);
    case MENU_AUDIO: return 1 + p->naudio;
    }
    return 0;
}

static pbox menu_box(const player_view *p)
{
    const float h = menu_rows(p) * MENU_ROW + 16;
    const float x = p->is_short ? gear_box(p).x - MENU_W - 12 : ui_width() - 24 - MENU_W;
    const float bottom = p->is_short ? UI_H - 24 : UI_H - 70;
    return mk(x, bottom - h, MENU_W, h);
}

static pbox menu_row_box(const player_view *p, int i)
{
    pbox m = menu_box(p);
    return mk(m.x, m.y + 8 + i * MENU_ROW, m.w, MENU_ROW);
}

static void cc_value(const player_view *p, char *out, size_t n)
{
    if (!p->cc_available)
        snprintf(out, n, "Not available");
    else if (!p->cc_on)
        snprintf(out, n, "Off");
    else
        snprintf(out, n, "%s", p->cc_label[0] ? p->cc_label : "On");
}

static void menu_label(const player_view *p, int i, char *label, size_t ln, char *value, size_t vn, int *checked)
{
    *checked = 0;
    value[0] = 0;
    if (p->menu == MENU_MAIN) {
        if (i == 0) {
            snprintf(label, ln, "Quality");
            snprintf(value, vn, "%dp", p->quality);
        } else if (i == 1) {
            snprintf(label, ln, "Captions (1)");
            cc_value(p, value, vn);
        } else {
            snprintf(label, ln, "Audio track");
            ui_ascii(value, vn, p->audio_names[p->audio_sel]);
        }
        return;
    }
    if (i == 0) {
        snprintf(label, ln, "%s", p->menu == MENU_QUALITY ? "Quality" : p->menu == MENU_CAPTIONS ? "Captions" : "Audio track");
        return;
    }
    i--;
    if (p->menu == MENU_QUALITY) {
        snprintf(label, ln, "%dp", p->qualities[i]);
        *checked = p->qualities[i] == p->quality;
    } else if (p->menu == MENU_CAPTIONS) {
        if (i == 0) {
            snprintf(label, ln, "Off");
            *checked = !p->cc_on;
        } else {
            snprintf(label, ln, "%s", p->cc_label[0] ? p->cc_label : "English");
            *checked = p->cc_on;
        }
        if (!p->cc_available)
            snprintf(label, ln, "No captions for this video");
    } else {
        ui_ascii(label, ln, p->audio_names[i]);
        *checked = i == p->audio_sel;
    }
}

static void open_sub(player_view *p, int sub)
{
    p->menu = sub;
    p->menu_sel = 1;
    if (sub == MENU_QUALITY) {
        int i;
        for (i = 0; i < p->nqualities; i++)
            if (p->qualities[i] == p->quality)
                p->menu_sel = 1 + i;
    } else if (sub == MENU_CAPTIONS) {
        p->menu_sel = p->cc_on ? 2 : 1;
    } else if (sub == MENU_AUDIO) {
        p->menu_sel = 1 + p->audio_sel;
    }
}

/* A on menu row i. */
static int menu_choose(player_view *p, int i)
{
    if (p->menu == MENU_MAIN) {
        open_sub(p, i == 0 ? MENU_QUALITY : i == 1 ? MENU_CAPTIONS : MENU_AUDIO);
        return ACT_NONE;
    }
    if (i == 0) { /* header: back */
        int from = p->menu;
        p->menu = MENU_MAIN;
        p->menu_sel = from == MENU_QUALITY ? 0 : from == MENU_CAPTIONS ? 1 : 2;
        return ACT_NONE;
    }
    i--;
    if (p->menu == MENU_QUALITY) {
        p->menu = MENU_CLOSED;
        if (p->qualities[i] == p->quality)
            return ACT_NONE;
        p->want_quality = p->qualities[i];
        return ACT_PLAYER_SET_QUALITY;
    }
    if (p->menu == MENU_CAPTIONS) {
        p->menu = MENU_CLOSED;
        if (!p->cc_available || (i == 1) == (p->cc_on != 0))
            return ACT_NONE;
        return ACT_PLAYER_CAPTIONS;
    }
    p->menu = MENU_CLOSED;
    if (i == p->audio_sel)
        return ACT_NONE;
    p->want_audio = i;
    return ACT_PLAYER_SET_AUDIO;
}

static int menu_update(player_view *p, const ui_input *in)
{
    const unsigned k = in->pressed;
    const int n = menu_rows(p);
    int hover = -1, i;
    if (in->pointer) {
        for (i = 0; i < n; i++)
            if (in_box(menu_row_box(p, i), in->px, in->py))
                hover = i;
        if (hover >= 0 && in->pointer_moved)
            p->menu_sel = hover;
    }
    if (k & (UI_B | UI_2)) {
        if (p->menu != MENU_MAIN && !(k & UI_2))
            menu_choose(p, 0);
        else
            p->menu = MENU_CLOSED;
        return ACT_NONE;
    }
    if (k & UI_HOME) {
        p->menu = MENU_CLOSED;
        return ACT_PLAYER_BACK;
    }
    if (k & UI_UP && p->menu_sel > 0)
        p->menu_sel--;
    if (k & UI_DOWN && p->menu_sel < n - 1)
        p->menu_sel++;
    if (k & UI_LEFT && p->menu != MENU_MAIN)
        return menu_choose(p, 0);
    if (k & UI_RIGHT && p->menu == MENU_MAIN)
        return menu_choose(p, p->menu_sel);
    if (k & UI_A) {
        if (in->pointer && hover < 0) {
            if (!in_box(menu_box(p), in->px, in->py))
                p->menu = MENU_CLOSED; /* clicked elsewhere */
            return ACT_NONE;
        }
        return menu_choose(p, in->pointer ? hover : p->menu_sel);
    }
    return ACT_NONE;
}

/* ---- input ---- */

static void notice(player_view *p, const char *msg)
{
    snprintf(p->notice, sizeof p->notice, "%s", msg);
    p->notice_timer = 90;
}

static void nudge(player_view *p, double d)
{
    if (!p->can_seek) {
        notice(p, "Skipping isn't available for this video");
        return;
    }
    if (p->nudge_target < 0) {
        p->nudge_target = p->position;
        p->nudge_from = p->position;
    }
    p->nudge_target += d;
    if (p->duration > 1 && p->nudge_target > p->duration - 1)
        p->nudge_target = p->duration - 1;
    if (p->nudge_target < 0)
        p->nudge_target = 0;
    p->nudge_timer = NUDGE_WAIT;
}

static int set_volume(player_view *p, int v)
{
    p->volume = v < 0 ? 0 : v > 255 ? 255 : v;
    p->volume_timer = 120;
    return ACT_PLAYER_VOLUME;
}

static int overlay_shown(const player_view *p)
{
    return p->overlay_timer > 0 || p->paused || p->menu != MENU_CLOSED || p->nudge_target >= 0;
}

int player_update(player_view *p, const ui_input *in)
{
    const unsigned k = in->pressed;
    pbox bar = bar_box(p);
    int act = ACT_NONE;

    p->frame++;
    if (p->overlay_timer > 0)
        p->overlay_timer--;
    if (p->volume_timer > 0)
        p->volume_timer--;
    if (p->notice_timer > 0)
        p->notice_timer--;
    if (k || in->pointer_moved)
        p->overlay_timer = 180;

    /* A collected skip is carried out once the presses stop. */
    if (p->nudge_timer > 0 && --p->nudge_timer == 0 && p->nudge_target >= 0) {
        p->seek_to = p->nudge_target;
        p->nudge_target = -1;
        act = ACT_PLAYER_SEEK;
    }

    if (p->menu != MENU_CLOSED) {
        int a = menu_update(p, in);
        p->overlay_timer = 180;
        return a != ACT_NONE ? a : act;
    }

    /* Pointer over the progress bar: preview position; A jumps there. */
    p->hover_bar = -1;
    if (in->pointer && overlay_shown(p) && in->px >= bar.x - 4 && in->px < bar.x + bar.w + 4 &&
        in->py >= bar.y - (p->is_short ? 14 : 12) && in->py < bar.y + 14) {
        float f = (in->px - bar.x) / bar.w;
        p->hover_bar = f < 0 ? 0 : f > 1 ? 1 : f;
    }

    if (k & (UI_B | UI_HOME))
        return ACT_PLAYER_BACK;
    if (k & UI_2) {
        p->menu = MENU_MAIN;
        p->menu_sel = 0;
        return act;
    }
    if (k & UI_1) {
        if (!p->cc_available) {
            notice(p, "No captions for this video");
            return act;
        }
        return ACT_PLAYER_CAPTIONS;
    }
    if (k & (UI_PLUS | UI_MINUS))
        return set_volume(p, p->volume + ((k & UI_PLUS) ? 32 : -32));
    if (p->is_short) {
        if (k & UI_DOWN && p->has_next)
            return ACT_PLAYER_NEXT;
        if (k & UI_UP && p->has_prev)
            return ACT_PLAYER_PREV;
    } else if (k & (UI_UP | UI_DOWN)) {
        return set_volume(p, p->volume + ((k & UI_UP) ? 32 : -32));
    }
    if (k & UI_LEFT)
        nudge(p, -NUDGE_SECONDS);
    if (k & UI_RIGHT)
        nudge(p, NUDGE_SECONDS);
    /* Nunchuk stick: skip while held (repeats every half second). */
    if (in->stick_x > 0.6f || in->stick_x < -0.6f) {
        if (p->stick_timer-- <= 0) {
            nudge(p, in->stick_x > 0 ? NUDGE_SECONDS : -NUDGE_SECONDS);
            p->stick_timer = 30;
            p->overlay_timer = 180;
        }
    } else {
        p->stick_timer = 0;
    }

    if (in->pointer && (in->held & UI_A) && in_box(volume_box(), in->px, in->py) && !p->is_short)
        act = set_volume(p, (int)((in->px - 88) / 52 * 255));
    if (k & UI_A) {
        if (in->pointer) {
            if (p->hover_bar >= 0 && p->duration > 0) {
                if (!p->can_seek) {
                    notice(p, "Skipping isn't available for this video");
                    return act;
                }
                p->seek_to = p->hover_bar * p->duration;
                p->nudge_target = -1;
                p->nudge_timer = 0;
                return ACT_PLAYER_SEEK;
            }
            if (in_box(back_box(), in->px, in->py))
                return ACT_PLAYER_BACK;
            if (in_box(gear_box(p), in->px, in->py)) {
                p->menu = MENU_MAIN;
                p->menu_sel = 0;
                return act;
            }
            if (in_box(cc_box(p), in->px, in->py)) {
                if (!p->cc_available) {
                    notice(p, "No captions for this video");
                    return act;
                }
                return ACT_PLAYER_CAPTIONS;
            }
            if (p->is_short && in_box(updown_box(0), in->px, in->py))
                return p->has_prev ? ACT_PLAYER_PREV : act;
            if (p->is_short && in_box(updown_box(1), in->px, in->py))
                return p->has_next ? ACT_PLAYER_NEXT : act;
            if (!p->is_short && in_box(volume_box(), in->px, in->py))
                return act;
        }
        return ACT_PLAYER_PAUSE;
    }
    return act;
}

/* ---- drawing ---- */

/* Break text into lines that fit max_w (at spaces; '\n' forces a break). */
static int break_lines(const char *text, int font, float max_w, char lines[][200], int max_lines)
{
    int n = 0;
    const char *s = text;
    while (*s && n < max_lines) {
        char buf[200];
        size_t len = 0, last_space = 0;
        while (*s == ' ')
            s++;
        while (s[len] && s[len] != '\n' && len < sizeof buf - 1) {
            buf[len] = s[len];
            buf[len + 1] = 0;
            if (ui_text_width(font, buf) > max_w) {
                buf[len] = 0;
                break;
            }
            if (s[len] == ' ')
                last_space = len;
            len++;
        }
        if (s[len] && s[len] != '\n' && last_space > 0)
            len = last_space;
        if (len == 0 && s[0] && s[0] != '\n')
            len = 1;
        memcpy(lines[n], s, len);
        lines[n][len] = 0;
        while (len > 0 && lines[n][len - 1] == ' ')
            lines[n][--len] = 0;
        if (lines[n][0])
            n++;
        s += len;
        if (*s == '\n')
            s++;
    }
    return n;
}

static void draw_captions(const player_view *p, int controls)
{
    char lines[4][200];
    const int font = FONT_BIG, lh = ui_line_height(FONT_BIG) + 4;
    float cx, max_w, bottom;
    int n, i;
    if (!p->cc_on || !p->cc_text[0] || p->menu != MENU_CLOSED)
        return;
    if (p->is_short) {
        pbox c = short_column();
        cx = c.x + c.w / 2;
        max_w = c.w - 24;
        bottom = UI_H - 110;
    } else {
        cx = ui_width() / 2.0f;
        max_w = ui_width() * 0.8f;
        bottom = controls ? UI_H - 76 : UI_H - 28;
    }
    n = break_lines(p->cc_text, font, max_w, lines, 4);
    for (i = 0; i < n; i++) {
        const float w = ui_text_width(font, lines[i]);
        const float y = bottom - (n - i) * lh;
        dr_fill(cx - w / 2 - 8, y, w + 16, lh, 0x000000bf);
        ui_text(font, cx - w / 2, y + 2, lines[i], C_TEXT, 0);
    }
}

static void draw_bar(const player_view *p)
{
    const pbox b = bar_box(p);
    const int hot = p->hover_bar >= 0;
    const float th = p->is_short ? 3 : hot ? 6 : 4;
    const float y = b.y + (4 - th) / 2;
    double shown = p->nudge_target >= 0 ? p->nudge_target : p->position;
    float played = p->duration > 0 ? (float)(shown / p->duration) : 0;
    float buf = p->buffered > 1 ? 1 : p->buffered;
    played = played < 0 ? 0 : played > 1 ? 1 : played;
    dr_fill(b.x, y, b.w, th, 0xffffff4c);
    dr_fill(b.x, y, b.w * buf, th, 0xffffff80);
    if (hot)
        dr_fill(b.x, y, b.w * p->hover_bar, th, 0xffffffb0);
    dr_fill(b.x, y, b.w * played, th, C_RED);
    if (!p->is_short || hot)
        ui_circle(b.x + b.w * played, y + th / 2, hot ? 8 : 7, C_RED);
    if (hot && p->duration > 0) {
        char t[16];
        float tw, x;
        fmt_time(p->hover_bar * p->duration, t, sizeof t);
        tw = ui_text_width(FONT_TITLE, t) + 16;
        x = b.x + b.w * p->hover_bar - tw / 2;
        if (x < 4)
            x = 4;
        if (x + tw > ui_width() - 4)
            x = ui_width() - 4 - tw;
        ui_round_rect(x, b.y - 34, tw, 24, 6, 0x000000d0);
        ui_text(FONT_TITLE, x + 8, b.y - 31, t, C_TEXT, 0);
    }
}

static void draw_menu(const player_view *p)
{
    const pbox m = menu_box(p);
    const int n = menu_rows(p);
    int i;
    ui_round_rect(m.x, m.y, m.w, m.h, 12, 0x1c1c1cf2);
    for (i = 0; i < n; i++) {
        const pbox r = menu_row_box(p, i);
        char label[80], value[80];
        int checked;
        menu_label(p, i, label, sizeof label, value, sizeof value, &checked);
        if (i == p->menu_sel)
            dr_fill(r.x, r.y, r.w, r.h, 0xffffff26);
        if (p->menu != MENU_MAIN && i == 0) {
            const atlas_rect *a = &atlas_icons[ICON_CHEVRON_RIGHT];
            dr_atlas(r.x + 10, r.y + 8, 18, 18, a->x + a->w, a->y, -a->w, a->h, C_TEXT);
            ui_text(FONT_TITLE, r.x + 34, r.y + 8, label, C_TEXT, r.w - 44);
            dr_fill(r.x, r.y + r.h - 1, r.w, 1, 0xffffff30);
            continue;
        }
        if (p->menu == MENU_MAIN) {
            const float vw = ui_text_width(FONT_SMALL, value);
            ui_text(FONT_TITLE, r.x + 16, r.y + 8, label, C_TEXT, 0);
            ui_text(FONT_SMALL, r.x + r.w - 34 - (vw > 150 ? 150 : vw), r.y + 9, value, C_TEXT2, 150);
            ui_icon(ICON_CHEVRON_RIGHT, r.x + r.w - 28, r.y + 8, 18, 18, C_TEXT2);
        } else {
            if (checked)
                ui_icon(ICON_CHECK, r.x + 12, r.y + 7, 20, 20, C_TEXT);
            ui_text(FONT_TITLE, r.x + 40, r.y + 8, label, C_TEXT, r.w - 52);
        }
    }
}

static void draw_round_button(pbox b, int icon, rgba_t bg, rgba_t fg)
{
    const float r = b.w / 2;
    ui_circle(b.x + r, b.y + r, r, bg);
    ui_icon(icon, b.x + r - 12, b.y + r - 12, 24, 24, fg);
}

static void draw_cc_button(pbox b, const player_view *p, int round)
{
    const rgba_t fg = p->cc_available ? C_TEXT : 0x777777ffu;
    if (round)
        ui_circle(b.x + b.w / 2, b.y + b.h / 2, b.w / 2, 0x272727ee);
    ui_round_rect(b.x + (b.w - 30) / 2, b.y + (b.h - 20) / 2, 30, 20, 4, fg);
    ui_round_rect(b.x + (b.w - 30) / 2 + 2, b.y + (b.h - 20) / 2 + 2, 26, 16, 3, round ? 0x272727ffu : 0x202020ffu);
    ui_text(FONT_META, b.x + (b.w - ui_text_width(FONT_META, "CC")) / 2, b.y + (b.h - 20) / 2 + 3, "CC", fg, 0);
    if (p->cc_on)
        dr_fill(b.x + (b.w - 22) / 2, b.y + b.h / 2 + 12, 22, 3, C_RED);
}

static void draw_center_badges(const player_view *p)
{
    const float W = (float)ui_width(), H = UI_H;
    const float cx = p->is_short ? short_column().x + short_column().w / 2 : W / 2;
    if (p->buffering && !p->paused) {
        ui_circle(cx, H / 2, 34, 0x00000088);
        ui_spinner(cx, H / 2, 20, p->frame);
    }
    if (p->nudge_target >= 0) {
        char t[48], tt[16];
        const double d = p->nudge_target - p->nudge_from;
        float tw;
        fmt_time(p->nudge_target, tt, sizeof tt);
        snprintf(t, sizeof t, "%s%d s  (%s)", d >= 0 ? "+" : "-", (int)(d >= 0 ? d + 0.5 : -d + 0.5), tt);
        tw = ui_text_width(FONT_BIG, t) + 32;
        ui_round_rect(cx - tw / 2, H / 2 - 20, tw, 40, 20, 0x000000b0);
        ui_text(FONT_BIG, cx - tw / 2 + 16, H / 2 - 11, t, C_TEXT, 0);
    } else if (p->paused) {
        ui_circle(cx, H / 2, 40, 0x00000099);
        ui_icon(ICON_PLAY, cx - 20, H / 2 - 22, 44, 44, C_TEXT);
    }
    if (p->notice_timer > 0 && p->notice[0]) {
        const float tw = ui_text_width(FONT_TITLE, p->notice) + 32;
        ui_round_rect(cx - tw / 2, 64, tw, 32, 16, ui_alpha(0x000000c0, p->notice_timer / 20.0f));
        ui_text(FONT_TITLE, cx - tw / 2 + 16, 71, p->notice, ui_alpha(C_TEXT, p->notice_timer / 20.0f), 0);
    }
}

static void draw_short_overlay(player_view *p, int show)
{
    const pbox c = short_column();
    char title[256], channel[128];
    ui_ascii(title, sizeof title, p->title);
    ui_ascii(channel, sizeof channel, p->channel);
    /* Title and channel over the bottom of the video, as on YouTube. */
    dr_vgradient(c.x, UI_H - 130, c.w, 130, 0x00000000, 0x000000c0);
    if (channel[0])
        ui_text(FONT_TITLE, c.x + 14, UI_H - 82, channel, C_TEXT, c.w - 28);
    ui_text_wrap(FONT_SMALL, c.x + 14, UI_H - 60, title, C_TEXT, c.w - 28, 2);
    draw_bar(p);
    draw_captions(p, show);

    /* Buttons beside the video. */
    if (p->has_prev) {
        pbox b = updown_box(0);
        const atlas_rect *a = &atlas_icons[ICON_CHEVRON];
        ui_circle(b.x + 22, b.y + 22, 22, 0x272727ee);
        dr_atlas(b.x + 10, b.y + 10, 24, 24, a->x, a->y + a->h, a->w, -a->h, C_TEXT);
    }
    if (p->has_next) {
        pbox b = updown_box(1);
        ui_circle(b.x + 22, b.y + 22, 22, 0x272727ee);
        ui_icon(ICON_CHEVRON, b.x + 10, b.y + 10, 24, 24, C_TEXT);
    }
    draw_cc_button(cc_box(p), p, 1);
    draw_round_button(gear_box(p), ICON_GEAR, 0x272727ee, C_TEXT);
    {
        pbox b = back_box();
        draw_round_button(b, ICON_BACK, 0x272727ee, C_TEXT);
    }
    if (show) {
        const char *hint = "Up / Down: previous / next     B: back";
        ui_text(FONT_META, c.x + c.w + 22, UI_H - 18, hint, C_TEXT2, ui_width() - (c.x + c.w + 30));
    }
    draw_center_badges(p);
    if (p->menu != MENU_CLOSED)
        draw_menu(p);
}

void player_draw_overlay(player_view *p)
{
    const float W = (float)ui_width(), H = UI_H;
    const int show = overlay_shown(p);
    char t1[16], t2[16], times[40], title[256], channel[128], q[16];

    if (p->is_short) {
        draw_short_overlay(p, show);
        return;
    }
    draw_captions(p, show);
    draw_center_badges(p);
    if (!show)
        return;

    /* Top: back button and title over a fade. */
    dr_vgradient(0, 0, W, 96, 0x000000c8, 0x00000000);
    ui_icon(ICON_BACK, back_box().x + 6, back_box().y + 6, 24, 24, C_TEXT);
    ui_ascii(title, sizeof title, p->title);
    ui_ascii(channel, sizeof channel, p->channel);
    ui_text(FONT_BIG, 54, 16, title, C_TEXT, W - 78);
    ui_text(FONT_SMALL, 54, 42, channel, C_TEXT2, W - 78);

    /* Bottom: progress bar and controls over a fade. */
    dr_vgradient(0, H - 120, W, 120, 0x00000000, 0x000000d8);
    draw_bar(p);
    ui_icon(p->paused ? ICON_PLAY : ICON_PAUSE, 22, H - 44, 26, 26, C_TEXT);
    ui_icon(ICON_VOLUME, 62, H - 42, 22, 22, C_TEXT);
    dr_fill(88, H - 32, 52, 3, 0xffffff4c);
    dr_fill(88, H - 32, 52.0f * p->volume / 255, 3, C_TEXT);
    ui_circle(88 + 52.0f * p->volume / 255, H - 30.5f, 5, C_TEXT);
    fmt_time(p->position, t1, sizeof t1);
    fmt_time(p->duration, t2, sizeof t2);
    snprintf(times, sizeof times, "%s / %s", t1, t2);
    ui_text(FONT_SMALL, 156, H - 40, times, C_TEXT, 0);
    {
        const pbox g = gear_box(p), cc = cc_box(p);
        float qw, qx;
        const char *hint = "< >: skip 10 s    1: captions    2: settings";
        ui_icon(ICON_GEAR, g.x + 3, g.y + 3, 24, 24, C_TEXT);
        draw_cc_button(cc, p, 0);
        snprintf(q, sizeof q, "%dp", p->quality);
        qw = ui_text_width(FONT_TITLE, q) + 16;
        qx = cc.x - 10 - qw;
        ui_round_rect(qx, H - 42, qw, 22, 4, 0xffffff26);
        ui_text(FONT_TITLE, qx + 8, H - 40, q, C_TEXT, 0);
        if (qx - 12 - ui_text_width(FONT_META, hint) > 156 + ui_text_width(FONT_SMALL, times) + 12)
            ui_text(FONT_META, qx - 12 - ui_text_width(FONT_META, hint), H - 38, hint, C_TEXT2, 0);
    }
    if (p->menu != MENU_CLOSED)
        draw_menu(p);
}
