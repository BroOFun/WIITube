#include <gccore.h>
#include <malloc.h>
#include <string.h>

#include "gfx.h"

#define FIFO_SIZE (256 * 1024)
#define CON_MARGIN_X 32
#define CON_MARGIN_Y 24

static GXRModeObj *rmode;
static void *con_xfb;
static void *xfb[2];
static int cur_fb;
static int widescreen;
static int con_cols, con_rows;

static GXTexObj video_tex, white_tex;
static int have_tex;
static int frame_w, frame_h, tex_w, tex_h;
static u16 white_pixels[16] __attribute__((aligned(32)));

void gfx_init(void)
{
    void *fifo;
    f32 yscale;
    u32 xfb_height;
    Mtx44 proj;
    Mtx view;
    GXColor black = { 0, 0, 0, 0xff };
    int i;

    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;

    con_xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_ClearFrameBuffer(rmode, con_xfb, COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);

    /* Text console with a margin for TV overscan; the font is 8x16. */
    CON_Init(con_xfb, CON_MARGIN_X, CON_MARGIN_Y, rmode->fbWidth - 2 * CON_MARGIN_X,
             rmode->xfbHeight - 2 * CON_MARGIN_Y, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    con_cols = (rmode->fbWidth - 2 * CON_MARGIN_X) / 8;
    con_rows = (rmode->xfbHeight - 2 * CON_MARGIN_Y) / 16;

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(con_xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    /* GX setup (from the standard libogc template). */
    fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);
    GX_SetCopyClear(black, 0x00ffffff);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    if (rmode->aa)
        GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else
        GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(xfb[0], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    /* One vertex format for everything: 2D position, colour, texture coordinate.
     * Output = texture x vertex colour; solid shapes use a white texture. */
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    guOrtho(proj, 0, rmode->efbHeight, 0, rmode->fbWidth, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0, 0, -1.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    for (i = 0; i < 16; i++)
        white_pixels[i] = 0xffff;
    DCFlushRange(white_pixels, sizeof white_pixels);
    GX_InitTexObj(&white_tex, white_pixels, 4, 4, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
}

int gfx_widescreen(void) { return widescreen; }
int gfx_screen_width(void) { return rmode->fbWidth; }
int gfx_console_x(void) { return CON_MARGIN_X; }
int gfx_console_y(void) { return CON_MARGIN_Y; }

/* The framebuffer stores pixel pairs as Y0 Cb Y1 Cr (one u32 per two pixels). */
int gfx_console_image(const unsigned char *rgba, int w, int h, int x, int y)
{
    u32 *fb = (u32 *)con_xfb;
    const int stride = rmode->fbWidth / 2;
    const int dw = widescreen ? w * 3 / 4 : w;
    int dx, dy;

    x &= ~1;
    for (dy = 0; dy < h; dy++) {
        const int py = y + dy;
        if (py < 0 || py >= rmode->xfbHeight)
            continue;
        for (dx = 0; dx + 1 < dw; dx += 2) {
            const int px = x + dx;
            int sx0 = dx * w / dw, sx1 = (dx + 1) * w / dw;
            const unsigned char *p0, *p1;
            int r0, g0, b0, r1, g1, b1, r, g, b, y0, y1, cb, cr;
            if (px < 0 || px + 1 >= rmode->fbWidth)
                continue;
            if (sx1 >= w)
                sx1 = w - 1;
            p0 = rgba + (dy * w + sx0) * 4;
            p1 = rgba + (dy * w + sx1) * 4;
            r0 = p0[0] * p0[3] / 255; g0 = p0[1] * p0[3] / 255; b0 = p0[2] * p0[3] / 255;
            r1 = p1[0] * p1[3] / 255; g1 = p1[1] * p1[3] / 255; b1 = p1[2] * p1[3] / 255;
            r = (r0 + r1) / 2; g = (g0 + g1) / 2; b = (b0 + b1) / 2;
            y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
            y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
            cb = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            cr = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            fb[py * stride + px / 2] = ((u32)y0 << 24) | ((u32)cb << 16) | ((u32)y1 << 8) | (u32)cr;
        }
    }
    return dw;
}
int gfx_console_cols(void) { return con_cols; }
int gfx_console_rows(void) { return con_rows; }

void gfx_show_console(void)
{
    VIDEO_SetNextFramebuffer(con_xfb);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

void gfx_video_begin(int fw, int fh, int tw, int th)
{
    frame_w = fw;
    frame_h = fh;
    tex_w = tw;
    tex_h = th;
    have_tex = 0;
}

void gfx_video_set_texture(const uint16_t *tex)
{
    GX_InitTexObj(&video_tex, (void *)tex, (u16)tex_w, (u16)tex_h, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&video_tex, GX_LINEAR, GX_LINEAR);
    GX_InvalidateTexAll();
    have_tex = 1;
}

static void quad(f32 x, f32 y, f32 w, f32 h, f32 s1, f32 t1, u8 r, u8 g, u8 b, u8 a)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_Color4u8(r, g, b, a);
    GX_TexCoord2f32(0, 0);
    GX_Position2f32(x + w, y);
    GX_Color4u8(r, g, b, a);
    GX_TexCoord2f32(s1, 0);
    GX_Position2f32(x + w, y + h);
    GX_Color4u8(r, g, b, a);
    GX_TexCoord2f32(s1, t1);
    GX_Position2f32(x, y + h);
    GX_Color4u8(r, g, b, a);
    GX_TexCoord2f32(0, t1);
    GX_End();
}

void gfx_video_present(const gfx_overlay *ov)
{
    const f32 W = rmode->fbWidth, H = rmode->efbHeight;

    if (have_tex && frame_w > 0 && frame_h > 0) {
        /* Fit the picture to the screen, keeping its shape. The framebuffer is always
         * 640 wide; the TV stretches it to 4:3 or 16:9. */
        f32 screen_aspect = widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
        f32 video_aspect = (f32)frame_w / (f32)frame_h;
        f32 dw = W, dh = H;
        if (video_aspect > screen_aspect)
            dh = H * screen_aspect / video_aspect;
        else
            dw = W * video_aspect / screen_aspect;
        GX_LoadTexObj(&video_tex, GX_TEXMAP0);
        quad((W - dw) / 2, (H - dh) / 2, dw, dh, (f32)frame_w / tex_w, (f32)frame_h / tex_h, 255, 255, 255, 255);
    }
    if (ov && (ov->visible || ov->paused)) {
        const f32 bx = 40, bw = W - 80, by = H - 56, bh = 8;
        GX_LoadTexObj(&white_tex, GX_TEXMAP0);
        quad(bx - 8, by - 12, bw + 16, bh + 24, 1, 1, 0, 0, 0, 160);
        quad(bx, by, bw, bh, 1, 1, 90, 90, 90, 255);
        quad(bx, by, bw * ov->buffered, bh, 1, 1, 170, 170, 170, 255);
        quad(bx, by, bw * ov->progress, bh, 1, 1, 230, 30, 30, 255);
        if (ov->paused) {
            quad(W / 2 - 30, H / 2 - 40, 20, 80, 1, 1, 255, 255, 255, 220);
            quad(W / 2 + 10, H / 2 - 40, 20, 80, 1, 1, 255, 255, 255, 220);
        }
    }
    GX_DrawDone();
    cur_fb ^= 1;
    GX_CopyDisp(xfb[cur_fb], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(xfb[cur_fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

void gfx_video_end(void)
{
    have_tex = 0;
    GX_DrawDone();
}
