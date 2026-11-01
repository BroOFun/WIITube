/* Text and shape helpers built on the drawing primitives. */
#include "ui.h"

#include <string.h>

/* ---- text ---------------------------------------------------------------------- */

static const char *translit(unsigned cp)
{
    static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    static char one[2];
    if (cp >= 0xC0 && cp <= 0xFF) {
        one[0] = latin1[cp - 0xC0];
        one[1] = 0;
        return one;
    }
    switch (cp) {
    case 0x2018: case 0x2019: case 0x00B4: return "'";
    case 0x201C: case 0x201D: return "\"";
    case 0x2010: case 0x2013: case 0x2014: return "-";
    case 0x2022: case 0x00B7: return "\x7f"; /* bullet glyph */
    case 0x2026: return "...";
    case 0x00A0: case 0x2009: case 0x200A: case 0x202F: return " ";
    case 0x00A9: return "(c)";
    case 0x00AE: return "(R)";
    case 0x2122: return "TM";
    }
    if (cp >= 0x1F000 || (cp >= 0x2600 && cp < 0x2800) || cp == 0xFE0F || cp == 0x200D || cp == 0x200B)
        return "";
    return "?";
}

void ui_ascii(char *dst, size_t dstlen, const char *src)
{
    const unsigned char *s = (const unsigned char *)src;
    size_t o = 0;
    if (!dstlen)
        return;
    while (s && *s && o + 1 < dstlen) {
        unsigned cp;
        int more;
        const char *rep;
        if (*s < 0x80) {
            dst[o++] = *s >= 0x20 ? (char)*s : ' ';
            s++;
            continue;
        }
        if ((*s & 0xE0) == 0xC0) { cp = *s & 0x1F; more = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s & 0x0F; more = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s & 0x07; more = 3; }
        else { s++; continue; }
        s++;
        while (more-- > 0 && (*s & 0xC0) == 0x80)
            cp = (cp << 6) | (*s++ & 0x3F);
        for (rep = translit(cp); *rep && o + 1 < dstlen; rep++)
            dst[o++] = *rep;
    }
    dst[o] = 0;
}

static const atlas_glyph *glyph(int font, unsigned char ch)
{
    if (ch < 32 || ch > 127)
        ch = '?';
    return &atlas_fonts[font].glyphs[ch - 32];
}

int ui_line_height(int font) { return atlas_fonts[font].line_height; }

static float width_n(int font, const char *s, size_t n)
{
    float w = 0;
    size_t i;
    for (i = 0; i < n && s[i]; i++)
        w += glyph(font, (unsigned char)s[i])->advance;
    return w;
}

float ui_text_width(int font, const char *s) { return width_n(font, s, (size_t)-1); }

static float draw_n(int font, float x, float y, const char *s, size_t n, rgba_t c)
{
    const float baseline = (float)(int)(y + atlas_fonts[font].ascent);
    float pen = (float)(int)(x + 0.5f);
    size_t i;
    for (i = 0; i < n && s[i]; i++) {
        const atlas_glyph *g = glyph(font, (unsigned char)s[i]);
        if (g->w > 0)
            dr_atlas(pen + g->xoff, baseline + g->yoff, g->w, g->h, g->x, g->y, g->w, g->h, c);
        pen += g->advance;
    }
    return pen - x;
}

float ui_text(int font, float x, float y, const char *s, rgba_t c, float max_w)
{
    size_t n = strlen(s);
    if (max_w > 0 && width_n(font, s, n) > max_w) {
        const float dots = ui_text_width(font, "...");
        while (n > 0 && width_n(font, s, n) + dots > max_w)
            n--;
        while (n > 0 && s[n - 1] == ' ')
            n--;
        return draw_n(font, x, y, s, n, c) + draw_n(font, x + width_n(font, s, n), y, "...", 3, c);
    }
    return draw_n(font, x, y, s, n, c);
}

/* Word-wrap; align 0 = left edge at x, 1 = each line centred on x. */
static int wrap(int font, float x, float y, const char *s, rgba_t c, float max_w, int max_lines, int center)
{
    int line = 0;
    const int lh = ui_line_height(font);
    while (*s && line < max_lines) {
        size_t fit = 0, i = 0, last_space = 0;
        float lx = x;
        while (*s == ' ')
            s++;
        /* Longest prefix that fits, broken at a space when possible. */
        while (s[i]) {
            if (width_n(font, s, i + 1) > max_w)
                break;
            i++;
            if (s[i] == ' ' || s[i] == 0)
                last_space = i;
        }
        fit = s[i] == 0 ? i : (last_space ? last_space : (i ? i : 1));
        if (line == max_lines - 1 && s[fit]) {
            /* Last line: cut with "..." */
            if (center)
                lx = x - (width_n(font, s, (size_t)-1) > max_w ? max_w : width_n(font, s, (size_t)-1)) / 2;
            ui_text(font, lx, y + line * lh, s, c, max_w);
            return line + 1;
        }
        if (center)
            lx = x - width_n(font, s, fit) / 2;
        draw_n(font, lx, y + line * lh, s, fit, c);
        s += fit;
        line++;
    }
    return line;
}

int ui_text_wrap(int font, float x, float y, const char *s, rgba_t c, float max_w, int max_lines)
{
    return wrap(font, x, y, s, c, max_w, max_lines, 0);
}

int ui_text_wrap_center(int font, float cx, float y, const char *s, rgba_t c, float max_w, int max_lines)
{
    return wrap(font, cx, y, s, c, max_w, max_lines, 1);
}

/* ---- shapes -------------------------------------------------------------------- */

rgba_t ui_alpha(rgba_t c, float a)
{
    unsigned al = (unsigned)((c & 0xff) * (a < 0 ? 0 : a > 1 ? 1 : a) + 0.5f);
    return (c & 0xffffff00u) | al;
}

void ui_icon(int icon, float x, float y, float w, float h, rgba_t c)
{
    const atlas_rect *r = &atlas_icons[icon];
    dr_atlas(x, y, w, h, r->x, r->y, r->w, r->h, c);
}

void ui_circle(float cx, float cy, float r, rgba_t c)
{
    ui_icon(ICON_CIRCLE, cx - r, cy - r, 2 * r, 2 * r, c);
}

void ui_round_rect(float x, float y, float w, float h, float r, rgba_t c)
{
    const atlas_rect *ci = &atlas_icons[ICON_CIRCLE];
    const float half = ci->w / 2.0f;
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r < 1) {
        dr_fill(x, y, w, h, c);
        return;
    }
    dr_atlas(x, y, r, r, ci->x, ci->y, half, half, c);
    dr_atlas(x + w - r, y, r, r, ci->x + half, ci->y, half, half, c);
    dr_atlas(x, y + h - r, r, r, ci->x, ci->y + half, half, half, c);
    dr_atlas(x + w - r, y + h - r, r, r, ci->x + half, ci->y + half, half, half, c);
    dr_fill(x + r, y, w - 2 * r, h, c);
    dr_fill(x, y + r, r, h - 2 * r, c);
    dr_fill(x + w - r, y + r, r, h - 2 * r, c);
}

void ui_round_corners(float x, float y, float w, float h, float r, rgba_t bg)
{
    const atlas_rect *m = &atlas_icons[ICON_CORNER_INV];
    dr_atlas(x, y, r, r, m->x, m->y, m->w, m->h, bg);
    dr_atlas(x + w - r, y, r, r, m->x + m->w, m->y, -m->w, m->h, bg);
    dr_atlas(x, y + h - r, r, r, m->x, m->y + m->h, m->w, -m->h, bg);
    dr_atlas(x + w - r, y + h - r, r, r, m->x + m->w, m->y + m->h, -m->w, -m->h, bg);
}

void ui_spinner(float cx, float cy, float r, unsigned frame)
{
    /* Eight dots around a circle; the bright one moves round (no libm needed). */
    static const float dx[8] = { 0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f };
    static const float dy[8] = { -1, -0.7071f, 0, 0.7071f, 1, 0.7071f, 0, -0.7071f };
    const int head = (int)(frame / 5) % 8;
    int i;
    for (i = 0; i < 8; i++) {
        int age = (head - i + 8) % 8;
        ui_circle(cx + dx[i] * r, cy + dy[i] * r, r * 0.16f, ui_alpha(C_TEXT, 1.0f - age * 0.11f));
    }
}

void ui_cursor(float x, float y)
{
    ui_icon(ICON_CURSOR_EDGE, x - 4, y - 3, 36, 36, 0x000000ffu);
    ui_icon(ICON_CURSOR_FILL, x - 2, y - 1, 32, 32, 0xffffffffu);
}
