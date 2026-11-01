/* GX backend for the UI drawing API (src/ui/draw.h). */
#include <gccore.h>
#include <malloc.h>
#include <string.h>

#include "../../core/yuv_tev.h"
#include "../../ui/atlas.h"
#include "../../ui/draw.h"
#include "gfx.h"

#define FIFO_SIZE (256 * 1024)

static GXRModeObj *rmode;
static void *xfb[2];
static int cur_fb;
static int widescreen, vw;
static GXTexObj atlas_tex;
static u8 *atlas_tiled;
static float white_u, white_v; /* texcoord inside the atlas' solid block */

enum { MODE_NONE, MODE_MASK, MODE_IMAGE };
static int cur_mode;
static const void *cur_tex;

int ui_width(void) { return vw; }
int gfx_widescreen(void) { return widescreen; }

static void tile_atlas(void)
{
    /* GX_TF_I8: 8x4-texel tiles, 32 bytes each, tiles left-to-right then top-to-bottom. */
    int tx, ty, x, y, k = 0;
    atlas_tiled = memalign(32, (size_t)atlas_w * atlas_h);
    for (ty = 0; ty < atlas_h / 4; ty++)
        for (tx = 0; tx < atlas_w / 8; tx++)
            for (y = 0; y < 4; y++)
                for (x = 0; x < 8; x++)
                    atlas_tiled[k++] = atlas_i8[(size_t)(ty * 4 + y) * atlas_w + tx * 8 + x];
    DCFlushRange(atlas_tiled, (u32)atlas_w * atlas_h);
    GX_InitTexObj(&atlas_tex, atlas_tiled, (u16)atlas_w, (u16)atlas_h, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&atlas_tex, GX_LINEAR, GX_LINEAR);
    white_u = (atlas_icons[ICON_WHITE].x + 2.0f) / atlas_w;
    white_v = (atlas_icons[ICON_WHITE].y + 2.0f) / atlas_h;
}

void gfx_init(void)
{
    void *fifo;
    f32 yscale;
    u32 xfb_height;
    GXColor bg = { 15, 15, 15, 0xff };

    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    widescreen = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    vw = widescreen ? 854 : 640;

    xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
    VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);
    GX_SetCopyClear(bg, 0x00ffffff);
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(xfb[0], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    /* Every quad: 2D position (virtual canvas), colour, texture coordinate. */
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
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);

    {
        /* The virtual canvas (vw x 480) maps onto the whole framebuffer. */
        Mtx44 proj;
        Mtx view;
        guOrtho(proj, 0, UI_H, 0, vw, 0, 300);
        GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
        guMtxIdentity(view);
        guMtxTransApply(view, view, 0, 0, -1.0f);
        GX_LoadPosMtxImm(view, GX_PNMTX0);
        GX_SetCurrentMtx(GX_PNMTX0);
    }
    tile_atlas();
}

/* Texture colour ignored; alpha = texture x vertex. For text and shapes from the atlas. */
static void use_mask(void)
{
    if (cur_mode != MODE_MASK) {
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        cur_mode = MODE_MASK;
    }
    if (cur_tex != atlas_tiled) {
        GX_LoadTexObj(&atlas_tex, GX_TEXMAP0);
        cur_tex = atlas_tiled;
    }
}

void gfx_frame_begin(void)
{
    GX_InvalidateTexAll(); /* thumbnails/video frames may have changed in memory */
    cur_mode = MODE_NONE;
    cur_tex = NULL;
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
}

void gfx_frame_end(void)
{
    GX_DrawDone();
    cur_fb ^= 1;
    GX_CopyDisp(xfb[cur_fb], GX_TRUE);
    GX_Flush();
    VIDEO_SetNextFramebuffer(xfb[cur_fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

static void quad(float x, float y, float w, float h, float s0, float t0, float s1, float t1, rgba_t top, rgba_t bottom)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_Color1u32(top);
    GX_TexCoord2f32(s0, t0);
    GX_Position2f32(x + w, y);
    GX_Color1u32(top);
    GX_TexCoord2f32(s1, t0);
    GX_Position2f32(x + w, y + h);
    GX_Color1u32(bottom);
    GX_TexCoord2f32(s1, t1);
    GX_Position2f32(x, y + h);
    GX_Color1u32(bottom);
    GX_TexCoord2f32(s0, t1);
    GX_End();
}

void dr_fill(float x, float y, float w, float h, rgba_t c)
{
    if (w <= 0 || h <= 0)
        return;
    use_mask();
    quad(x, y, w, h, white_u, white_v, white_u, white_v, c, c);
}

void dr_vgradient(float x, float y, float w, float h, rgba_t top, rgba_t bottom)
{
    if (w <= 0 || h <= 0)
        return;
    use_mask();
    quad(x, y, w, h, white_u, white_v, white_u, white_v, top, bottom);
}

void dr_atlas(float x, float y, float w, float h, float ax, float ay, float aw, float ah, rgba_t c)
{
    if (w <= 0 || h <= 0)
        return;
    use_mask();
    quad(x, y, w, h, ax / atlas_w, ay / atlas_h, (ax + aw) / atlas_w, (ay + ah) / atlas_h, c, c);
}

void dr_image(const wt_image *img, float x, float y, float w, float h, uint8_t alpha)
{
    dr_image_part(img, x, y, w, h, 0, 0, 1, 1, alpha);
}

void dr_image_part(const wt_image *img, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                   uint8_t alpha)
{
    static GXTexObj obj;
    rgba_t c = 0xffffff00u | alpha;
    if (!img || !img->tex || w <= 0 || h <= 0)
        return;
    if (cur_mode != MODE_IMAGE) {
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        cur_mode = MODE_IMAGE;
    }
    if (cur_tex != img->tex) {
        GX_InitTexObj(&obj, img->tex, (u16)img->tw, (u16)img->th, img->fmt == IMG_RGB5A3 ? GX_TF_RGB5A3 : GX_TF_RGB565,
                      GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjFilterMode(&obj, GX_LINEAR, GX_LINEAR);
        GX_LoadTexObj(&obj, GX_TEXMAP0);
        cur_tex = img->tex;
    }
    {
        const float sx = (float)img->w / img->tw, sy = (float)img->h / img->th;
        quad(x, y, w, h, u0 * sx, v0 * sy, u1 * sx, v1 * sy, c, c);
    }
}

/* One stage of the YUV->RGB combiner: P = d op (konst-or-ras * texture), see yuv_tev.h. */
static void yuv_stage(u8 stage, u8 coord, u8 map, u8 ksel, u8 cin, u8 din, u8 op, u8 scale, u8 clamp)
{
    GX_SetTevOrder(stage, coord, map, GX_COLOR0A0);
    GX_SetTevKColorSel(stage, ksel);
    GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC, cin, din);
    GX_SetTevColorOp(stage, op, GX_TB_ZERO, scale, clamp, GX_TEVPREV);
    GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

void dr_video(const yuv_frame *f, float x, float y, float w, float h)
{
    static GXTexObj ty, tu, tv;
    static yuv_tev_consts consts[2];
    static int have_consts;
    const yuv_tev_consts *k;
    GXColorS10 c0;
    GXColor kc;
    float ys, yt, cs, ct;
    const u32 col = 0x0000ffffu; /* the vertex colour supplies stage 3's (0, 0, 1) */

    if (!f || w <= 0 || h <= 0)
        return;
    if (!have_consts) {
        yuv_tev_constants(0, &consts[0]);
        yuv_tev_constants(1, &consts[1]);
        have_consts = 1;
    }
    k = &consts[f->bt709 ? 1 : 0];
    ys = (float)f->w / f->yw;
    yt = (float)f->h / f->yh;
    cs = (float)f->cw / f->cwt;
    ct = (float)f->ch / f->cht;

    GX_InitTexObj(&ty, (void *)f->y, (u16)f->yw, (u16)f->yh, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObj(&tu, (void *)f->u, (u16)f->cwt, (u16)f->cht, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObj(&tv, (void *)f->v, (u16)f->cwt, (u16)f->cht, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&ty, GX_LINEAR, GX_LINEAR);
    GX_InitTexObjFilterMode(&tu, GX_LINEAR, GX_LINEAR);
    GX_InitTexObjFilterMode(&tv, GX_LINEAR, GX_LINEAR);
    GX_LoadTexObj(&ty, GX_TEXMAP0);
    GX_LoadTexObj(&tu, GX_TEXMAP1);
    GX_LoadTexObj(&tv, GX_TEXMAP2);

    c0.r = k->c0[0];
    c0.g = k->c0[1];
    c0.b = k->c0[2];
    c0.a = 0;
    GX_SetTevColorS10(GX_TEVREG0, c0);
    kc.r = k->k5[0]; kc.g = k->k5[1]; kc.b = k->k5[2]; kc.a = k->ky; /* K0: rgb for stage 5, alpha = Y */
    GX_SetTevKColor(GX_KCOLOR0, kc);
    kc.r = k->k1[0]; kc.g = k->k1[1]; kc.b = k->k1[2]; kc.a = 0;
    GX_SetTevKColor(GX_KCOLOR1, kc);
    kc.r = k->k2[0]; kc.g = k->k2[1]; kc.b = k->k2[2];
    GX_SetTevKColor(GX_KCOLOR2, kc);
    kc.r = k->k3[0]; kc.g = k->k3[1]; kc.b = k->k3[2];
    GX_SetTevKColor(GX_KCOLOR3, kc);

    /* Luma uses texture coordinate 0, chroma (half size, own padding) coordinate 1. */
    GX_SetNumTexGens(2);
    GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY);
    GX_SetVtxDesc(GX_VA_TEX1, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
    GX_SetNumTevStages(6);
    yuv_stage(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_TEV_KCSEL_K0_A, GX_CC_KONST, GX_CC_C0, GX_TEV_ADD, GX_CS_SCALE_1, GX_FALSE);
    yuv_stage(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP2, GX_TEV_KCSEL_K1, GX_CC_KONST, GX_CC_CPREV, GX_TEV_ADD, GX_CS_SCALE_1, GX_FALSE);
    yuv_stage(GX_TEVSTAGE2, GX_TEXCOORD1, GX_TEXMAP2, GX_TEV_KCSEL_K2, GX_CC_KONST, GX_CC_CPREV, GX_TEV_SUB, GX_CS_SCALE_1, GX_FALSE);
    yuv_stage(GX_TEVSTAGE3, GX_TEXCOORD1, GX_TEXMAP1, GX_TEV_KCSEL_K0, GX_CC_RASC, GX_CC_CPREV, GX_TEV_ADD, GX_CS_SCALE_1, GX_FALSE);
    yuv_stage(GX_TEVSTAGE4, GX_TEXCOORD1, GX_TEXMAP1, GX_TEV_KCSEL_K3, GX_CC_KONST, GX_CC_CPREV, GX_TEV_ADD, GX_CS_SCALE_1, GX_FALSE);
    yuv_stage(GX_TEVSTAGE5, GX_TEXCOORD1, GX_TEXMAP1, GX_TEV_KCSEL_K0, GX_CC_KONST, GX_CC_CPREV, GX_TEV_SUB, GX_CS_SCALE_2, GX_TRUE);

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_Color1u32(col);
    GX_TexCoord2f32(0, 0);
    GX_TexCoord2f32(0, 0);
    GX_Position2f32(x + w, y);
    GX_Color1u32(col);
    GX_TexCoord2f32(ys, 0);
    GX_TexCoord2f32(cs, 0);
    GX_Position2f32(x + w, y + h);
    GX_Color1u32(col);
    GX_TexCoord2f32(ys, yt);
    GX_TexCoord2f32(cs, ct);
    GX_Position2f32(x, y + h);
    GX_Color1u32(col);
    GX_TexCoord2f32(0, yt);
    GX_TexCoord2f32(0, ct);
    GX_End();

    /* Back to the one-stage setup used by everything else. */
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetNumTexGens(1);
    GX_SetVtxDesc(GX_VA_TEX1, GX_NONE);
    cur_mode = MODE_NONE;
    cur_tex = NULL;
}

void dr_clip(float x, float y, float w, float h)
{
    const float sx = (float)rmode->fbWidth / vw, sy = (float)rmode->efbHeight / UI_H;
    if (w <= 0 || h <= 0) {
        GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    GX_SetScissor((u32)(x * sx), (u32)(y * sy), (u32)(w * sx), (u32)(h * sy));
}
