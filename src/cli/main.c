/* wiitube-cli: desktop test harness for the portable core (same code the Wii app will use). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/http.h"
#include "../core/yt.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: wiitube-cli [--rules FILE] COMMAND ...\n"
            "  fetch URL                 HTTPS GET; print status and size (connectivity test)\n"
            "  search QUERY...           search YouTube\n"
            "  info VIDEO                list streams (VIDEO = id or URL)\n"
            "  get VIDEO [-q HEIGHT] [-o NAME]\n"
            "                            download H.264 video <= HEIGHT (default 240) + AAC audio\n");
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
    http_response_free(&resp);
    return resp.status >= 200 && resp.status < 400 ? 0 : 1;
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
    if (strcmp(argv[i], "fetch") == 0 && i + 1 < argc)
        return cmd_fetch(argv[i + 1]);
    if (strcmp(argv[i], "search") == 0 && i + 1 < argc)
        return cmd_search(argc - i - 1, argv + i + 1);
    if (strcmp(argv[i], "info") == 0 && i + 1 < argc)
        return cmd_info(argv[i + 1]);
    if (strcmp(argv[i], "get") == 0 && i + 1 < argc)
        return cmd_get(argc - i - 1, argv + i + 1);
    usage();
    return 2;
}
