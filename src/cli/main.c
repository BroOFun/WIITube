/* wiitube-cli: desktop test harness for the portable core (same code the Wii app will use). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/captions.h"
#include "../core/http.h"
#include "../core/yt.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: wiitube-cli [--rules FILE] COMMAND ...\n"
            "  fetch URL...              HTTPS GET each URL; print status and size (connectivity test)\n"
            "  search QUERY...           search YouTube\n"
            "  info VIDEO                list streams (VIDEO = id or URL)\n"
            "  get VIDEO [-q HEIGHT] [-o NAME]\n"
            "                            download H.264 video <= HEIGHT (default 240) + AAC audio\n"
            "  seek VIDEO SECONDS [-q HEIGHT] [-o NAME]\n"
            "                            download the streams from SECONDS on the way the Wii seeks\n"
            "                            (header + data from the seek-index piece containing SECONDS)\n"
            "  captions VIDEO [SECONDS]  list caption tracks; print the English caption at SECONDS\n");
}

typedef struct {
    FILE *f;
    long long bytes;
    long long total;
    int last_pct;
} dl_state;

static int write_cb(void *ud, const unsigned char *data, size_t len)
{
    dl_state *s = ud;
    if (s->f && fwrite(data, 1, len, s->f) != len)
        return -1;
    s->bytes += (long long)len;
    if (s->total > 0) {
        int pct = (int)(s->bytes * 100 / s->total);
        if (pct / 10 != s->last_pct / 10) {
            fprintf(stderr, "  %d%%\n", pct);
            s->last_pct = pct;
        }
    }
    return 0;
}

static int cmd_fetch(const char *url)
{
    http_request req;
    http_response resp;
    dl_state st = { NULL, 0, 0, 0 };
    char err[512];
    int ok;

    memset(&req, 0, sizeof req);
    req.url = url;
    req.headers = "User-Agent: wiitube-cli/0.1\r\n";
    req.on_body = write_cb;
    req.ud = &st;
    if (http_do(&req, &resp, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    printf("HTTP %d, %lld bytes, content-type: %s\n", resp.status, st.bytes, resp.content_type);
    ok = resp.status >= 200 && resp.status < 400;
    http_response_free(&resp);
    return ok ? 0 : 1;
}

static int cmd_search(int argc, char **argv)
{
    char query[512] = "", err[512];
    yt_search_result res[30];
    int i, n;

    for (i = 0; i < argc; i++) {
        size_t l = strlen(query);
        snprintf(query + l, sizeof query - l, "%s%s", l ? " " : "", argv[i]);
    }
    n = yt_search(query, res, 30, err, sizeof err);
    if (n < 0) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    for (i = 0; i < n; i++)
        printf("%-11s  %7s  %-60.60s  %s\n", res[i].video_id, res[i].duration, res[i].title, res[i].channel);
    if (n == 0)
        fprintf(stderr, "no results%s%s\n", err[0] ? ": " : "", err);
    return n > 0 ? 0 : 1;
}

static int load_video(const char *arg, yt_video *v)
{
    char id[16], err[1024];
    if (yt_parse_video_id(arg, id) != 0) {
        fprintf(stderr, "error: not a video ID or URL: %s\n", arg);
        return -1;
    }
    if (yt_get_video(id, v, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", err);
        return -1;
    }
    return 0;
}

static int cmd_info(const char *arg)
{
    yt_video v;
    int i;

    if (load_video(arg, &v) != 0)
        return 1;
    printf("%s  \"%s\" by %s, %d s (client: %s)\n", v.video_id, v.title, v.channel, v.length_seconds, v.client);
    printf("itag  type        codecs                     res        fps  kbps    size     url\n");
    for (i = 0; i < v.nformats; i++) {
        const yt_format *f = &v.formats[i];
        char res[16] = "audio";
        if (f->has_video)
            snprintf(res, sizeof res, "%dx%d", f->width, f->height);
        printf("%-4d  %-10s  %-25.25s  %-9s  %3d  %5d  %7.1fM  %s%s\n", f->itag, f->mime, f->codecs, res, f->fps,
               f->bitrate / 1000, f->content_length > 0 ? f->content_length / 1048576.0 : 0.0,
               f->url ? "direct" : "ciphered", f->has_video && f->has_audio ? " (muxed)" : "");
    }
    if (v.hls_url)
        printf("HLS manifest available\n");
    for (i = 0; i < v.nformats; i++)
        if (v.formats[i].audio_id[0])
            printf("audio track: itag %d id %s \"%s\"%s%s\n", v.formats[i].itag, v.formats[i].audio_id,
                   v.formats[i].audio_name, v.formats[i].audio_default ? " default" : "", v.formats[i].is_drc ? " drc" : "");
    for (i = 0; i < v.ncaptions; i++)
        printf("captions: %s \"%s\"%s\n", v.captions[i].lang, v.captions[i].name, v.captions[i].asr ? " (auto)" : "");
    yt_video_free(&v);
    return 0;
}

static int download_one(const yt_video *v, const yt_format *f, const char *path)
{
    dl_state st;
    char err[512];
    FILE *out = fopen(path, "wb");
    if (!out) {
        perror(path);
        return -1;
    }
    memset(&st, 0, sizeof st);
    st.f = out;
    st.total = f->content_length;
    fprintf(stderr, "downloading itag %d (%s %s) -> %s\n", f->itag, f->mime, f->codecs, path);
    if (yt_download(v, f, write_cb, &st, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", err);
        fclose(out);
        return -1;
    }
    fclose(out);
    fprintf(stderr, "  done, %lld bytes\n", st.bytes);
    return 0;
}

static int cmd_get(int argc, char **argv)
{
    const char *name = NULL;
    int height = 240, i, rc = 1;
    const yt_format *vf, *af;
    yt_video v;
    char path[512];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            name = argv[++i];
    }
    if (load_video(argv[0], &v) != 0)
        return 1;
    if (!name)
        name = v.video_id;
    if (yt_pick_formats(&v, height, &vf, &af) != 0) {
        fprintf(stderr, "error: no H.264 + AAC streams at or below %dp\n", height);
        goto out;
    }
    snprintf(path, sizeof path, "%s.%s.mp4", name, af ? "video" : "muxed");
    if (download_one(&v, vf, path) != 0)
        goto out;
    if (af) {
        snprintf(path, sizeof path, "%s.audio.m4a", name);
        if (download_one(&v, af, path) != 0)
            goto out;
    }
    rc = 0;
out:
    yt_video_free(&v);
    return rc;
}

/* Write bytes [0, init_end] and [offset, end) of f to path (what yt_stream_start_at sends). */
static int download_from(const yt_video *v, const yt_format *f, long long offset, const char *path)
{
    dl_state st;
    char err[512];
    FILE *out = fopen(path, "wb");
    int rc;
    if (!out) {
        perror(path);
        return -1;
    }
    memset(&st, 0, sizeof st);
    st.f = out;
    fprintf(stderr, "downloading itag %d header (0-%lld) + %lld-%lld -> %s\n", f->itag, f->init_end, offset,
            f->content_length, path);
    rc = yt_download_span(f->url, 0, f->init_end + 1, v->user_agent, write_cb, &st, err, sizeof err);
    if (rc == 0)
        rc = yt_download_span(f->url, offset, f->content_length, v->user_agent, write_cb, &st, err, sizeof err);
    fclose(out);
    if (rc != 0)
        fprintf(stderr, "error: %s\n", err);
    return rc;
}

static int cmd_seek(int argc, char **argv)
{
    const char *name = NULL;
    int height = 240, i, rc = 1, k, ka;
    double t;
    const yt_format *vf, *af;
    yt_index vidx, aidx;
    yt_video v;
    char path[512], err[512];

    if (argc < 2)
        return 1;
    t = atof(argv[1]);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            name = argv[++i];
    }
    if (load_video(argv[0], &v) != 0)
        return 1;
    memset(&vidx, 0, sizeof vidx);
    memset(&aidx, 0, sizeof aidx);
    if (!name)
        name = v.video_id;
    if (yt_pick_formats(&v, height, &vf, &af) != 0 || !af) {
        fprintf(stderr, "error: no separate H.264 + AAC streams at or below %dp\n", height);
        goto out;
    }
    if (yt_fetch_index(&v, vf, &vidx, err, sizeof err) != 0 || yt_fetch_index(&v, af, &aidx, err, sizeof err) != 0) {
        fprintf(stderr, "error: seek index: %s\n", err);
        goto out;
    }
    k = yt_index_find(&vidx, t);
    ka = yt_index_find(&aidx, vidx.start[k]);
    printf("video: %d pieces, %.2f s; %.1f s is in piece %d, which starts at %.3f s (byte %lld)\n", vidx.n,
           vidx.start[vidx.n], t, k, vidx.start[k], vidx.offset[k]);
    printf("audio: %d pieces; piece %d starts at %.3f s (byte %lld)\n", aidx.n, ka, aidx.start[ka], aidx.offset[ka]);
    printf("expect: video %.3f audio %.3f\n", vidx.start[k], aidx.start[ka]);
    snprintf(path, sizeof path, "%s.video.mp4", name);
    if (download_from(&v, vf, vidx.offset[k], path) != 0)
        goto out;
    snprintf(path, sizeof path, "%s.audio.m4a", name);
    if (download_from(&v, af, aidx.offset[ka], path) != 0)
        goto out;
    rc = 0;
out:
    yt_index_free(&vidx);
    yt_index_free(&aidx);
    yt_video_free(&v);
    return rc;
}

static int cmd_captions(int argc, char **argv)
{
    yt_video v;
    cc_track t;
    char err[512], buf[512];
    int i, translate = -1, pick, rc = 1;
    if (load_video(argv[0], &v) != 0)
        return 1;
    for (i = 0; i < v.ncaptions; i++)
        printf("track %d: %s \"%s\"%s\n", i, v.captions[i].lang, v.captions[i].name, v.captions[i].asr ? " (auto)" : "");
    pick = cc_pick_english(&v, &translate);
    if (pick < 0 && translate < 0) {
        printf("no captions\n");
        yt_video_free(&v);
        return 0;
    }
    if (cc_fetch(&v, pick >= 0 ? pick : translate, pick >= 0 ? NULL : "en", &t, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", err);
    } else {
        double at = argc > 1 ? atof(argv[1]) : 20;
        const char *text = cc_text_at(&t, at, buf, sizeof buf);
        printf("using \"%s\": %d cues; at %.1f s: %s\n", t.label, t.n, at, text ? text : "(nothing)");
        rc = t.n > 0 ? 0 : 1;
        cc_free(&t);
    }
    yt_video_free(&v);
    return rc;
}

int main(int argc, char **argv)
{
    const char *rules = "rules/youtube.json";
    char err[512];
    int i = 1;

    if (i + 1 < argc && strcmp(argv[i], "--rules") == 0) {
        rules = argv[i + 1];
        i += 2;
    }
    if (i >= argc) {
        usage();
        return 2;
    }
    if (yt_load_rules(rules, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    if (strcmp(argv[i], "fetch") == 0 && i + 1 < argc) {
        int k, rc = 0, opened, reused;
        for (k = i + 1; k < argc; k++)
            rc |= cmd_fetch(argv[k]);
        http_stats(&opened, &reused);
        printf("connections: %d opened, %d reused\n", opened, reused);
        return rc;
    }
    if (strcmp(argv[i], "search") == 0 && i + 1 < argc)
        return cmd_search(argc - i - 1, argv + i + 1);
    if (strcmp(argv[i], "info") == 0 && i + 1 < argc)
        return cmd_info(argv[i + 1]);
    if (strcmp(argv[i], "dump") == 0 && i + 1 < argc) {
        int cmd_dump(const char *dir);
        return cmd_dump(argv[i + 1]);
    }
    if (strcmp(argv[i], "get") == 0 && i + 1 < argc)
        return cmd_get(argc - i - 1, argv + i + 1);
    if (strcmp(argv[i], "seek") == 0 && i + 2 < argc)
        return cmd_seek(argc - i - 1, argv + i + 1);
    if (strcmp(argv[i], "captions") == 0 && i + 1 < argc)
        return cmd_captions(argc - i - 1, argv + i + 1);
    usage();
    return 2;
}
