/* FFmpeg-based demux/decode of one fragmented-MP4 stream read from a ring buffer.
 * The input is treated as non-seekable, so the mov demuxer reads it front to back. */
#include "media.h"
#include "log.h"

#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#define AVIO_BUF_SIZE 32768

struct media {
    ring *r;
    int kind;
    AVIOContext *pb;
    AVFormatContext *fmt;
    AVCodecContext *dec;
    AVStream *st;
    AVPacket *pkt;
    AVFrame *frame;
    int eof_sent;
    double last_pts;
    double expect_start; /* < 0: not seeking */
    double shift;        /* added to timestamps (see media_expect_start) */
    int got_first;
};

static void av_log_to_wlog(void *avcl, int level, const char *fmt, va_list vl)
{
    char line[512];
    size_t n;
    (void)avcl;
    if (level > AV_LOG_WARNING)
        return;
    vsnprintf(line, sizeof line, fmt, vl);
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = 0;
    if (n)
        wlog("ffmpeg: %s", line);
}

static int read_cb(void *opaque, uint8_t *buf, int size)
{
    media *m = opaque;
    size_t n = ring_read(m->r, buf, (size_t)size);
    return n == 0 ? AVERROR_EOF : (int)n;
}

static void av_err(char *err, size_t errlen, const char *what, int rc)
{
    char buf[128];
    av_strerror(rc, buf, sizeof buf);
    snprintf(err, errlen, "%s: %s", what, buf);
}

media *media_open(ring *r, int kind, char *err, size_t errlen)
{
    static int log_installed;
    const enum AVMediaType want = kind == MEDIA_VIDEO ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
    const AVCodec *codec;
    unsigned char *iobuf;
    media *m;
    unsigned i;
    int rc;

    if (!log_installed) {
        av_log_set_callback(av_log_to_wlog);
        log_installed = 1;
    }
    m = av_mallocz(sizeof *m);
    if (!m)
        return NULL;
    m->r = r;
    m->kind = kind;
    m->expect_start = -1;
    iobuf = av_malloc(AVIO_BUF_SIZE);
    m->pb = iobuf ? avio_alloc_context(iobuf, AVIO_BUF_SIZE, 0, m, read_cb, NULL, NULL) : NULL;
    m->fmt = avformat_alloc_context();
    m->pkt = av_packet_alloc();
    m->frame = av_frame_alloc();
    if (!m->pb || !m->fmt || !m->pkt || !m->frame) {
        if (!m->pb)
            av_free(iobuf);
        snprintf(err, errlen, "out of memory");
        goto fail;
    }
    m->pb->seekable = 0;
    m->fmt->pb = m->pb;
    m->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    rc = avformat_open_input(&m->fmt, NULL, av_find_input_format("mov"), NULL);
    if (rc < 0) {
        av_err(err, errlen, ring_error(r) ? "download failed" : "cannot read stream header", rc);
        goto fail; /* avformat_open_input freed and cleared m->fmt */
    }
    for (i = 0; i < m->fmt->nb_streams; i++) {
        AVStream *st = m->fmt->streams[i];
        if (!m->st && st->codecpar->codec_type == want)
            m->st = st;
        else
            st->discard = AVDISCARD_ALL;
    }
    if (!m->st) {
        snprintf(err, errlen, "no %s track in stream", kind == MEDIA_VIDEO ? "video" : "audio");
        goto fail;
    }
    codec = avcodec_find_decoder(m->st->codecpar->codec_id);
    if (!codec) {
        snprintf(err, errlen, "no decoder for %s", avcodec_get_name(m->st->codecpar->codec_id));
        goto fail;
    }
    m->dec = avcodec_alloc_context3(codec);
    if (!m->dec || avcodec_parameters_to_context(m->dec, m->st->codecpar) < 0) {
        snprintf(err, errlen, "decoder setup failed");
        goto fail;
    }
    m->dec->thread_count = 1;
    if (kind == MEDIA_VIDEO)
        m->dec->flags2 |= AV_CODEC_FLAG2_FAST; /* allow FFmpeg's speed shortcuts (not bit-exact) */
    m->dec->pkt_timebase = m->st->time_base;
    rc = avcodec_open2(m->dec, codec, NULL);
    if (rc < 0) {
        av_err(err, errlen, "cannot open decoder", rc);
        goto fail;
    }
    wlog("media: opened %s %s %dx%d %d Hz", kind == MEDIA_VIDEO ? "video" : "audio", codec->name,
         m->dec->width, m->dec->height, m->dec->sample_rate);
    return m;

fail:
    media_close(m);
    return NULL;
}

void media_close(media *m)
{
    if (!m)
        return;
    avcodec_free_context(&m->dec);
    if (m->fmt)
        avformat_close_input(&m->fmt);
    if (m->pb) {
        av_freep(&m->pb->buffer);
        avio_context_free(&m->pb);
    }
    av_packet_free(&m->pkt);
    av_frame_free(&m->frame);
    av_free(m);
}

int media_width(const media *m) { return m->dec->width; }
int media_height(const media *m) { return m->dec->height; }
int media_sample_rate(const media *m) { return m->dec->sample_rate; }

double media_duration(const media *m)
{
    if (m->st->duration > 0)
        return (double)m->st->duration * av_q2d(m->st->time_base);
    if (m->fmt->duration > 0)
        return (double)m->fmt->duration / AV_TIME_BASE;
    return 0;
}

void media_set_skip(media *m, int level)
{
    m->dec->skip_loop_filter = level >= 1 ? AVDISCARD_ALL : AVDISCARD_DEFAULT;
    m->dec->skip_frame = level >= 2 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
}

/* Get the next decoded frame into m->frame. 1 = frame, 0 = end, < 0 = error. */
static int decode_next(media *m)
{
    for (;;) {
        int rc = avcodec_receive_frame(m->dec, m->frame);
        if (rc == 0)
            return 1;
        if (rc == AVERROR_EOF)
            return 0;
        if (rc != AVERROR(EAGAIN))
            return rc;
        if (m->eof_sent)
            return 0;
        rc = av_read_frame(m->fmt, m->pkt);
        if (rc < 0) {
            if (rc != AVERROR_EOF)
                wlog("media: read error %d", rc);
            avcodec_send_packet(m->dec, NULL); /* flush delayed frames */
            m->eof_sent = 1;
            continue;
        }
        if (m->pkt->stream_index == m->st->index) {
            rc = avcodec_send_packet(m->dec, m->pkt);
            if (rc < 0 && rc != AVERROR(EAGAIN))
                wlog("media: decode error %d (skipping packet)", rc);
        }
        av_packet_unref(m->pkt);
    }
}

void media_expect_start(media *m, double t) { m->expect_start = t; }

static double frame_pts(media *m, double fallback_step)
{
    int64_t ts = m->frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) {
        m->last_pts += fallback_step;
    } else {
        double t = (double)ts * av_q2d(m->st->time_base);
        if (!m->got_first && m->expect_start >= 0 && (t < m->expect_start - 2 || t > m->expect_start + 2)) {
            /* The fragments carry no absolute times (no tfdt), so the demuxer counted
             * from 0 after the jump: use the seek index time instead. */
            m->shift = m->expect_start - t;
            wlog("media: timestamps start at %.2f s after seeking to %.2f s; shifting", t, m->expect_start);
        }
        m->last_pts = t + m->shift;
    }
    m->got_first = 1;
    return m->last_pts;
}

int media_next_video(media *m, media_frame *out)
{
    AVFrame *f = m->frame;
    int rc = decode_next(m);
    if (rc <= 0)
        return rc;
    if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) {
        wlog("media: unsupported pixel format %d", f->format);
        return -1;
    }
    out->width = f->width;
    out->height = f->height;
    out->plane[0] = f->data[0];
    out->plane[1] = f->data[1];
    out->plane[2] = f->data[2];
    out->stride[0] = f->linesize[0];
    out->stride[1] = f->linesize[1];
    out->stride[2] = f->linesize[2];
    out->bt709 = f->colorspace == AVCOL_SPC_BT709 ||
                 (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 720);
    out->pts = frame_pts(m, 1.0 / 30);
    return 1;
}

static int16_t to_s16(float x)
{
    float v = x * 32767.0f;
    if (v >= 32767.0f)
        return 32767;
    if (v <= -32768.0f)
        return -32768;
    return (int16_t)(v >= 0 ? v + 0.5f : v - 0.5f);
}

int media_next_audio(media *m, int16_t *out, int max_frames, int *frames, double *pts)
{
    AVFrame *f = m->frame;
    int n, ch, i;
    int rc = decode_next(m);
    if (rc <= 0)
        return rc;
    n = f->nb_samples < max_frames ? f->nb_samples : max_frames;
    ch = f->ch_layout.nb_channels;
    switch (f->format) {
    case AV_SAMPLE_FMT_FLTP: {
        const float *l = (const float *)f->data[0];
        const float *r = (const float *)f->data[ch > 1 ? 1 : 0];
        for (i = 0; i < n; i++) {
            out[2 * i] = to_s16(l[i]);
            out[2 * i + 1] = to_s16(r[i]);
        }
        break;
    }
    case AV_SAMPLE_FMT_FLT: {
        const float *s = (const float *)f->data[0];
        for (i = 0; i < n; i++) {
            out[2 * i] = to_s16(s[i * ch]);
            out[2 * i + 1] = to_s16(s[i * ch + (ch > 1)]);
        }
        break;
    }
    case AV_SAMPLE_FMT_S16P: {
        const int16_t *l = (const int16_t *)f->data[0];
        const int16_t *r = (const int16_t *)f->data[ch > 1 ? 1 : 0];
        for (i = 0; i < n; i++) {
            out[2 * i] = l[i];
            out[2 * i + 1] = r[i];
        }
        break;
    }
    case AV_SAMPLE_FMT_S16: {
        const int16_t *s = (const int16_t *)f->data[0];
        for (i = 0; i < n; i++) {
            out[2 * i] = s[i * ch];
            out[2 * i + 1] = s[i * ch + (ch > 1)];
        }
        break;
    }
    default:
        wlog("media: unsupported sample format %d", f->format);
        return -1;
    }
    *frames = n;
    *pts = frame_pts(m, (double)n / (f->sample_rate > 0 ? f->sample_rate : 44100));
    return 1;
}
