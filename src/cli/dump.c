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

int cmd_dump(const char *dir)
{
    char body[2048];
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
    raw(dir, "search_shorts.json", "search", "web", "{\"query\":\"minecraft #shorts\"}");
    raw(dir, "home.json", "browse", "web", "{\"browseId\":\"FEwhat_to_watch\"}");
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
