/* wiitube-cli dump OUTDIR: saves real YouTube responses so features can be developed
 * against actual data (run on GitHub Actions; the dev container can't reach YouTube). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/http.h"
#include "../core/yt.h"
#include "cJSON.h"

static void save(const char *dir, const char *name, const void *data, size_t len)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(data, 1, len, f);
    fclose(f);
    printf("saved %s (%zu bytes)\n", name, len);
}

/* Depth-first search for the first value under key. */
static cJSON *find(cJSON *n, const char *key)
{
    cJSON *c;
    if (!n)
        return NULL;
    if (n->string && strcmp(n->string, key) == 0)
        return n;
    for (c = n->child; c; c = c->next) {
        cJSON *r = find(c, key);
        if (r)
            return r;
    }
    return NULL;
}

static char *raw(const char *dir, const char *name, const char *ep, const char *client, const char *body)
{
    char err[512];
    char *json = yt_raw(ep, client, body, err, sizeof err);
    if (!json) {
        printf("%s: FAILED %s\n", name, err);
        return NULL;
    }
    save(dir, name, json, strlen(json));
    return json;
}

static int fetch(const char *dir, const char *name, const char *url, const char *range)
{
    http_request req;
    http_response resp;
    char err[512], headers[256];
    memset(&req, 0, sizeof req);
    snprintf(headers, sizeof headers, "User-Agent: Mozilla/5.0\r\n%s%s%s", range ? "Range: bytes=" : "",
             range ? range : "", range ? "\r\n" : "");
    req.url = url;
    req.headers = headers;
    if (http_do(&req, &resp, err, sizeof err) != 0) {
        printf("%s: FAILED %s\n", name, err);
        return -1;
    }
    printf("%s: HTTP %d\n", name, resp.status);
    if (resp.body)
        save(dir, name, resp.body, resp.body_len);
    http_response_free(&resp);
    return 0;
}

static void player_extras(const char *dir, const char *prefix, const char *json)
{
    cJSON *r = cJSON_Parse(json), *tracks, *fmts, *f;
    char name[128], url[4096];
    if (!r)
        return;
    tracks = find(r, "captionTracks");
    if (tracks && cJSON_GetArraySize(tracks) > 0) {
        cJSON *t = cJSON_GetArrayItem(tracks, 0);
        cJSON *bu = cJSON_GetObjectItem(t, "baseUrl");
        if (cJSON_IsString(bu)) {
            snprintf(url, sizeof url, "%s&fmt=json3", bu->valuestring);
            snprintf(name, sizeof name, "%s_captions.json3", prefix);
            fetch(dir, name, url, NULL);
        }
    }
    /* Index (sidx) and init ranges of the first 480p H.264 video and first AAC audio. */
    fmts = find(r, "adaptiveFormats");
    for (f = fmts ? fmts->child : NULL; f; f = f->next) {
        cJSON *mt = cJSON_GetObjectItem(f, "mimeType"), *u = cJSON_GetObjectItem(f, "url");
        cJSON *ir = cJSON_GetObjectItem(f, "indexRange"), *h = cJSON_GetObjectItem(f, "height");
        const char *kind = NULL;
        static int got_v, got_a;
        if (!cJSON_IsString(mt) || !cJSON_IsString(u) || !ir)
            continue;
        if (!got_v && strstr(mt->valuestring, "avc1") && cJSON_IsNumber(h) && h->valueint <= 480 && h->valueint >= 360)
            kind = "video", got_v = 1;
        else if (!got_a && strstr(mt->valuestring, "mp4a"))
            kind = "audio", got_a = 1;
        if (kind) {
            char range[64];
            cJSON *s = cJSON_GetObjectItem(ir, "start"), *e = cJSON_GetObjectItem(ir, "end");
            snprintf(range, sizeof range, "0-%s", cJSON_IsString(e) ? e->valuestring : "4095");
            snprintf(name, sizeof name, "%s_%s_head.bin", prefix, kind);
            fetch(dir, name, u->valuestring, range);
            (void)s;
        }
    }
    cJSON_Delete(r);
}

static void b64(const unsigned char *in, size_t n, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i < n; i += 3) {
        unsigned v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}

/* Count distinct "videoId" values in a JSON text. */
static int count_ids(const char *json)
{
    int n = 0;
    const char *p = json;
    char seen[200][12];
    while (json && (p = strstr(p, "\"videoId\":\"")) != NULL && n < 200) {
        int i, dup = 0;
        p += 11;
        for (i = 0; i < n; i++)
            if (strncmp(seen[i], p, 11) == 0)
                dup = 1;
        if (!dup) {
            memcpy(seen[n], p, 11);
            seen[n++][11] = 0;
        }
    }
    return n;
}

int cmd_dump(const char *dir)
{
    char body[8192];
    char *j;
    cJSON *r, *tok;

    raw(dir, "search_videos.json", "search", "web", "{\"query\":\"wii sports\",\"params\":\"EgIQAfABAQ==\"}");
    j = raw(dir, "search_all.json", "search", "web", "{\"query\":\"minecraft\"}");
    if (j && (r = cJSON_Parse(j)) != NULL) {
        tok = find(r, "token");
        if (cJSON_IsString(tok)) {
            snprintf(body, sizeof body, "{\"continuation\":\"%s\"}", tok->valuestring);
            raw(dir, "search_cont.json", "search", "web", body);
        }
        cJSON_Delete(r);
    }
    free(j);
    j = raw(dir, "search_shorts.json", "search", "web", "{\"query\":\"minecraft #shorts\"}");
    if (j && (r = cJSON_Parse(j)) != NULL) {
        /* YouTube's endless Shorts feed continues from a Short's sequenceParams. */
        tok = find(r, "sequenceParams");
        if (cJSON_IsString(tok)) {
            snprintf(body, sizeof body, "{\"sequenceParams\":\"%s\"}", tok->valuestring);
            if (strlen(body) < sizeof body - 1) {
                char *seq = raw(dir, "reel_seq.json", "reel/reel_watch_sequence", "web", body);
                cJSON *sr = seq ? cJSON_Parse(seq) : NULL;
                cJSON *ct = find(sr, "token");
                printf("Shorts feed: %d videos\n", count_ids(seq));
                if (cJSON_IsString(ct)) {
                    snprintf(body, sizeof body, "{\"continuation\":\"%s\"}", ct->valuestring);
                    raw(dir, "reel_seq_cont.json", "reel/reel_watch_sequence", "web", body);
                }
                cJSON_Delete(sr);
                free(seq);
            } else {
                printf("sequenceParams too long (%zu)\n", strlen(tok->valuestring));
            }
        }
        tok = find(r, "videoId");
        if (cJSON_IsString(tok)) {
            /* Can the Shorts feed start from any Short (sequenceParams = {1: videoId})? */
            unsigned char pb[13];
            char p64[32];
            char *seq;
            pb[0] = 0x0a;
            pb[1] = 11;
            memcpy(pb + 2, tok->valuestring, 11);
            b64(pb, 13, p64);
            snprintf(body, sizeof body, "{\"sequenceParams\":\"%s\"}", p64);
            seq = raw(dir, "reel_seq_built.json", "reel/reel_watch_sequence", "web", body);
            printf("Shorts feed from a built start (%s): %d videos\n", p64, count_ids(seq));
            free(seq);
        }
        if (cJSON_IsString(tok)) {
            snprintf(body, sizeof body, "{\"playerRequest\":{\"videoId\":\"%s\"},\"disablePlayerResponse\":true}",
                     tok->valuestring);
            raw(dir, "reel_item.json", "reel/reel_item_watch", "web", body);
        }
        cJSON_Delete(r);
    }
    free(j);
    raw(dir, "home.json", "browse", "web", "{\"browseId\":\"FEwhat_to_watch\"}");
    /* Other places YouTube recommends videos without an account. */
    raw(dir, "home_tv.json", "browse", "tv", "{\"browseId\":\"FEwhat_to_watch\"}");
    raw(dir, "home_tv_samsung.json", "browse", "tv_samsung", "{\"browseId\":\"FEwhat_to_watch\"}");
    raw(dir, "home_mweb.json", "browse", "mweb", "{\"browseId\":\"FEwhat_to_watch\"}");
    raw(dir, "trending.json", "browse", "web", "{\"browseId\":\"FEtrending\"}");
    raw(dir, "explore.json", "browse", "web", "{\"browseId\":\"FEexplore\"}");
    raw(dir, "next_rick.json", "next", "web", "{\"videoId\":\"dQw4w9WgXcQ\"}");
    raw(dir, "next_tv_rick.json", "next", "tv", "{\"videoId\":\"dQw4w9WgXcQ\"}");
    raw(dir, "shorts_tab.json", "browse", "web", "{\"browseId\":\"FEshorts\"}");
    j = raw(dir, "player_rick.json", "player", "visionos",
            "{\"videoId\":\"dQw4w9WgXcQ\",\"contentCheckOk\":true,\"racyCheckOk\":true}");
    if (j)
        player_extras(dir, "rick", j);
    free(j);
    j = raw(dir, "player_dub.json", "player", "visionos",
            "{\"videoId\":\"0e3GPea1Tyg\",\"contentCheckOk\":true,\"racyCheckOk\":true}");
    free(j);
    j = raw(dir, "player_tv_dub.json", "player", "tv_samsung",
            "{\"videoId\":\"0e3GPea1Tyg\",\"contentCheckOk\":true,\"racyCheckOk\":true}");
    free(j);
    return 0;
}
