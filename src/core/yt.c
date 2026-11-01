/* YouTube innertube client. The request shapes follow yt-dlp's YouTube extractor
 * (yt_dlp/extractor/youtube/_base.py and _video.py); client identities live in
 * rules/youtube.json so they can be updated without recompiling. */
#include "yt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

extern const char yt_rules_builtin[];

static cJSON *g_rules;
static char g_rules_source[256] = "built-in";
static char g_visitor_data[256];

#define YT_RANGE_CHUNK (10LL << 20)

/* ---- small JSON helpers ---------------------------------------------------- */

/* Walk a dotted path such as "a.b.0.c" (numbers index arrays). */
static cJSON *jpath(const cJSON *node, const char *path)
{
    char key[96];
    const char *p = path;
    cJSON *cur = (cJSON *)node;

    while (cur && *p) {
        size_t n = strcspn(p, ".");
        if (n >= sizeof key)
            return NULL;
        memcpy(key, p, n);
        key[n] = 0;
        p += n;
        if (*p == '.')
            p++;
        if (cJSON_IsArray(cur) && isdigit((unsigned char)key[0]))
            cur = cJSON_GetArrayItem(cur, atoi(key));
        else
            cur = cJSON_GetObjectItemCaseSensitive(cur, key);
    }
    return cur;
}

static const char *jstr(const cJSON *node, const char *path)
{
    cJSON *v = jpath(node, path);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double jnum(const cJSON *node, const char *path, double def)
{
    cJSON *v = jpath(node, path);
    if (cJSON_IsNumber(v))
        return v->valuedouble;
    if (cJSON_IsString(v))
        return atof(v->valuestring);
    return def;
}

/* YouTube text objects: {"simpleText": ...}, {"runs": [{"text": ...}]}, or {"content": ...}. */
static void jtext(const cJSON *node, char *out, size_t outlen)
{
    const cJSON *runs, *run;
    const char *s;
    size_t used = 0;

    out[0] = 0;
    if (!node)
        return;
    if ((s = jstr(node, "simpleText")) || (s = jstr(node, "content"))) {
        snprintf(out, outlen, "%s", s);
        return;
    }
    runs = jpath(node, "runs");
    cJSON_ArrayForEach(run, runs) {
        s = jstr(run, "text");
        if (s && used + 1 < outlen)
            used += (size_t)snprintf(out + used, outlen - used, "%s", s);
        if (used >= outlen)
            break;
    }
}

static void copy_str(char *dst, size_t dstlen, const char *src)
{
    snprintf(dst, dstlen, "%s", src ? src : "");
}

/* ---- rules ------------------------------------------------------------------ */

int yt_load_rules(const char *path, char *err, size_t errlen)
{
    cJSON *r = NULL;

    if (path) {
        FILE *f = fopen(path, "rb");
        if (f) {
            long n;
            char *buf;
            fseek(f, 0, SEEK_END);
            n = ftell(f);
            fseek(f, 0, SEEK_SET);
            buf = malloc((size_t)n + 1);
            if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
                buf[n] = 0;
                r = cJSON_Parse(buf);
                if (!r) {
                    snprintf(err, errlen, "rules file %s is not valid JSON", path);
                    free(buf);
                    fclose(f);
                    return -1;
                }
                snprintf(g_rules_source, sizeof g_rules_source, "%s", path);
            }
            free(buf);
            fclose(f);
        }
    }
    if (!r) {
        r = cJSON_Parse(yt_rules_builtin);
        snprintf(g_rules_source, sizeof g_rules_source, "built-in");
    }
    if (!r || !cJSON_IsObject(jpath(r, "clients"))) {
        snprintf(err, errlen, "rules are missing \"clients\"");
        cJSON_Delete(r);
        return -1;
    }
    cJSON_Delete(g_rules);
    g_rules = r;
    return 0;
}

int yt_load_rules_json(const char *json, const char *source, char *err, size_t errlen)
{
    cJSON *r = cJSON_Parse(json);
    if (!r || !cJSON_IsObject(jpath(r, "clients")) || !cJSON_IsArray(jpath(r, "player_clients"))) {
        snprintf(err, errlen, "rules from %s are not valid", source);
        cJSON_Delete(r);
        return -1;
    }
    cJSON_Delete(g_rules);
    g_rules = r;
    snprintf(g_rules_source, sizeof g_rules_source, "%s", source);
    return 0;
}

const char *yt_rules_source(void) { return g_rules_source; }

static int ensure_rules(char *err, size_t errlen)
{
    return g_rules ? 0 : yt_load_rules(NULL, err, errlen);
}

static const cJSON *rules_client(const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(jpath(g_rules, "clients"), key);
}

static const char *api_host(void)
{
    const char *h = jstr(g_rules, "api_host");
    return h ? h : "www.youtube.com";
}

/* ---- innertube requests ----------------------------------------------------- */

static cJSON *make_context(const cJSON *client)
{
    static const char *fields[] = { "clientName", "clientVersion", "deviceMake", "deviceModel",
                                    "osName", "osVersion", "userAgent", NULL };
    cJSON *ctx = cJSON_CreateObject();
    cJSON *c = cJSON_AddObjectToObject(ctx, "client");
    const cJSON *sdk;
    int i;

    for (i = 0; fields[i]; i++) {
        const char *v = jstr(client, fields[i]);
        if (v)
            cJSON_AddStringToObject(c, fields[i], v);
    }
    sdk = jpath(client, "androidSdkVersion");
    if (cJSON_IsNumber(sdk))
        cJSON_AddNumberToObject(c, "androidSdkVersion", sdk->valuedouble);
    cJSON_AddStringToObject(c, "hl", "en");
    cJSON_AddStringToObject(c, "gl", "US");
    cJSON_AddStringToObject(c, "timeZone", "UTC");
    cJSON_AddNumberToObject(c, "utcOffsetMinutes", 0);
    if (g_visitor_data[0])
        cJSON_AddStringToObject(c, "visitorData", g_visitor_data);
    return ctx;
}

/* POST /youtubei/v1/<ep>. body must not yet contain "context"; it is consumed. */
static cJSON *innertube(const char *ep, const char *client_key, cJSON *body, int *status_out, char *err,
                        size_t errlen)
{
    const cJSON *client = rules_client(client_key);
    char url[256], headers[1024];
    char *payload;
    http_request req;
    http_response resp;
    cJSON *out = NULL;
    const char *vd;

    if (!client) {
        snprintf(err, errlen, "unknown client \"%s\" in rules", client_key);
        cJSON_Delete(body);
        return NULL;
    }
    cJSON_AddItemToObject(body, "context", make_context(client));
    payload = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!payload)
        return NULL;

    snprintf(url, sizeof url, "https://%s/youtubei/v1/%s?prettyPrint=false", api_host(), ep);
    snprintf(headers, sizeof headers,
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-YouTube-Client-Name: %d\r\n"
             "X-YouTube-Client-Version: %s\r\n"
             "Origin: https://%s\r\n"
             "%s%s%s",
             jstr(client, "userAgent") ? jstr(client, "userAgent") : "Mozilla/5.0",
             (int)jnum(client, "clientNameId", 1), jstr(client, "clientVersion") ? jstr(client, "clientVersion") : "",
             api_host(), g_visitor_data[0] ? "X-Goog-Visitor-Id: " : "", g_visitor_data,
             g_visitor_data[0] ? "\r\n" : "");

    memset(&req, 0, sizeof req);
    req.method = "POST";
    req.url = url;
    req.headers = headers;
    req.body = payload;
    req.body_len = strlen(payload);
    if (http_do(&req, &resp, err, errlen) != 0) {
        free(payload);
        return NULL;
    }
    free(payload);
    if (status_out)
        *status_out = resp.status;
    if (resp.status != 200) {
        const char *msg = NULL;
        out = resp.body ? cJSON_Parse((char *)resp.body) : NULL;
        msg = jstr(out, "error.message");
        snprintf(err, errlen, "%s: HTTP %d%s%s", ep, resp.status, msg ? " - " : "", msg ? msg : "");
        cJSON_Delete(out);
        http_response_free(&resp);
        return NULL;
    }
    out = resp.body ? cJSON_Parse((char *)resp.body) : NULL;
    http_response_free(&resp);
    if (!out) {
        snprintf(err, errlen, "%s: response is not JSON", ep);
        return NULL;
    }
    vd = jstr(out, "responseContext.visitorData");
    if (vd && !g_visitor_data[0])
        copy_str(g_visitor_data, sizeof g_visitor_data, vd);
    return out;
}

/* ---- diagnostics ---------------------------------------------------------------- */

char *yt_raw(const char *ep, const char *client_key, const char *body_json, char *err, size_t errlen)
{
    cJSON *body, *resp;
    char *out;
    if (ensure_rules(err, errlen) != 0)
        return NULL;
    body = cJSON_Parse(body_json);
    if (!body) {
        snprintf(err, errlen, "request body is not JSON");
        return NULL;
    }
    resp = innertube(ep, client_key, body, NULL, err, errlen);
    if (!resp)
        return NULL;
    out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    return out;
}

/* ---- search ------------------------------------------------------------------ */

static int add_result(const cJSON *item, yt_search_result *out, int n, int max)
{
    const cJSON *vr = jpath(item, "videoRenderer");
    const cJSON *lvm = jpath(item, "lockupViewModel");
    yt_search_result *r;

    if (n >= max)
        return n;
    r = &out[n];
    memset(r, 0, sizeof *r);
    if (vr && jstr(vr, "videoId")) {
        copy_str(r->video_id, sizeof r->video_id, jstr(vr, "videoId"));
        jtext(jpath(vr, "title"), r->title, sizeof r->title);
        jtext(jpath(vr, "ownerText"), r->channel, sizeof r->channel);
        if (!r->channel[0])
            jtext(jpath(vr, "longBylineText"), r->channel, sizeof r->channel);
        jtext(jpath(vr, "lengthText"), r->duration, sizeof r->duration);
        jtext(jpath(vr, "shortViewCountText"), r->views, sizeof r->views);
        jtext(jpath(vr, "publishedTimeText"), r->published, sizeof r->published);
        return n + 1;
    }
    if (lvm && jstr(lvm, "contentId")) {
        const char *type = jstr(lvm, "contentType");
        if (type && strcmp(type, "LOCKUP_CONTENT_TYPE_VIDEO") != 0)
            return n;
        copy_str(r->video_id, sizeof r->video_id, jstr(lvm, "contentId"));
        jtext(jpath(lvm, "metadata.lockupMetadataViewModel.title"), r->title, sizeof r->title);
        return n + 1;
    }
    return n;
}

int yt_search(const char *query, yt_search_result *out, int max, char *err, size_t errlen)
{
    cJSON *body, *resp, *sections, *section, *item;
    const char *client_key, *params;
    int n = 0;

    if (ensure_rules(err, errlen) != 0)
        return -1;
    client_key = jstr(g_rules, "search.client");
    params = jstr(g_rules, "search.params");
    body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "query", query);
    if (params)
        cJSON_AddStringToObject(body, "params", params);
    resp = innertube("search", client_key ? client_key : "web", body, NULL, err, errlen);
    if (!resp)
        return -1;

    sections = jpath(resp, "contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents");
    cJSON_ArrayForEach(section, sections) {
        cJSON *items = jpath(section, "itemSectionRenderer.contents");
        cJSON_ArrayForEach(item, items)
            n = add_result(item, out, n, max);
    }
    if (n == 0 && !sections)
        snprintf(err, errlen, "search: unexpected response layout (YouTube may have changed it)");
    cJSON_Delete(resp);
    return n;
}

/* ---- endless feeds --------------------------------------------------------------- */

#define FEED_PARAMS_VIDEOS "EgIQAQ=="
#define FEED_PARAMS_SHORTS "EgIQCQ=="

static void feed_item(yt_feed *f, const cJSON *item, int depth)
{
    yt_search_result r;
    const cJSON *x, *c;
    const char *s;

    if (!item || depth > 4)
        return;
    memset(&r, 0, sizeof r);
    if ((x = jpath(item, "videoRenderer")) && jstr(x, "videoId")) {
        copy_str(r.video_id, sizeof r.video_id, jstr(x, "videoId"));
        jtext(jpath(x, "title"), r.title, sizeof r.title);
        jtext(jpath(x, "ownerText"), r.channel, sizeof r.channel);
        if (!r.channel[0])
            jtext(jpath(x, "longBylineText"), r.channel, sizeof r.channel);
        jtext(jpath(x, "lengthText"), r.duration, sizeof r.duration);
        jtext(jpath(x, "shortViewCountText"), r.views, sizeof r.views);
        jtext(jpath(x, "publishedTimeText"), r.published, sizeof r.published);
        s = jstr(x, "navigationEndpoint.commandMetadata.webCommandMetadata.url");
        r.is_short = s && strncmp(s, "/shorts/", 8) == 0;
    } else if ((x = jpath(item, "shortsLockupViewModel"))) {
        copy_str(r.video_id, sizeof r.video_id, jstr(x, "onTap.innertubeCommand.reelWatchEndpoint.videoId"));
        jtext(jpath(x, "overlayMetadata.primaryText"), r.title, sizeof r.title);
        jtext(jpath(x, "overlayMetadata.secondaryText"), r.views, sizeof r.views);
        copy_str(r.thumb_url, sizeof r.thumb_url, jstr(x, "thumbnailViewModel.thumbnailViewModel.image.sources.0.url"));
        if (!r.thumb_url[0])
            copy_str(r.thumb_url, sizeof r.thumb_url, jstr(x, "thumbnail.sources.0.url"));
        r.is_short = 1;
    } else if ((x = jpath(item, "reelItemRenderer")) && jstr(x, "videoId")) {
        copy_str(r.video_id, sizeof r.video_id, jstr(x, "videoId"));
        jtext(jpath(x, "headline"), r.title, sizeof r.title);
        jtext(jpath(x, "viewCountText"), r.views, sizeof r.views);
        copy_str(r.thumb_url, sizeof r.thumb_url, jstr(x, "thumbnail.thumbnails.0.url"));
        r.is_short = 1;
    } else if ((x = jpath(item, "lockupViewModel")) && jstr(x, "contentId")) {
        s = jstr(x, "contentType");
        if (s && strcmp(s, "LOCKUP_CONTENT_TYPE_VIDEO") != 0)
            return;
        copy_str(r.video_id, sizeof r.video_id, jstr(x, "contentId"));
        jtext(jpath(x, "metadata.lockupMetadataViewModel.title"), r.title, sizeof r.title);
    } else {
        /* Containers: shelves of videos or Shorts. */
        static const char *const lists[] = {
            "shelfRenderer.content.verticalListRenderer.items",
            "shelfRenderer.content.horizontalListRenderer.items",
            "gridShelfViewModel.contents",
            "reelShelfRenderer.items",
            "richItemRenderer.content",
            "richSectionRenderer.content.richShelfRenderer.contents",
            NULL,
        };
        int i;
        for (i = 0; lists[i]; i++) {
            const cJSON *l = jpath(item, lists[i]);
            if (cJSON_IsArray(l)) {
                cJSON_ArrayForEach(c, l)
                    feed_item(f, c, depth + 1);
            } else if (cJSON_IsObject(l)) {
                feed_item(f, l, depth + 1);
            }
        }
        return;
    }
    yt_feed_add(f, &r);
}

static void feed_sections(yt_feed *f, const cJSON *sections)
{
    const cJSON *sec, *item;
    free(f->continuation);
    f->continuation = NULL;
    cJSON_ArrayForEach(sec, sections) {
        const char *tok = jstr(sec, "continuationItemRenderer.continuationEndpoint.continuationCommand.token");
        if (tok) {
            free(f->continuation);
            f->continuation = strdup(tok);
            continue;
        }
        cJSON_ArrayForEach(item, jpath(sec, "itemSectionRenderer.contents"))
            feed_item(f, item, 0);
    }
}

static int feed_count(const yt_feed *f)
{
    return f->kind == YT_FEED_SHORTS ? f->nshorts : f->kind == YT_FEED_VIDEOS ? f->n : f->n + f->nshorts;
}

int yt_feed_start(yt_feed *f, const char *query, int kind, char *err, size_t errlen)
{
    cJSON *body, *resp;
    const char *client_key;
    yt_feed_free(f);
    f->kind = kind;
    copy_str(f->query, sizeof f->query, query);
    if (ensure_rules(err, errlen) != 0)
        return -1;
    client_key = jstr(g_rules, "search.client");
    body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "query", query);
    if (kind != YT_FEED_ALL)
        cJSON_AddStringToObject(body, "params", kind == YT_FEED_SHORTS ? FEED_PARAMS_SHORTS : FEED_PARAMS_VIDEOS);
    resp = innertube("search", client_key ? client_key : "web", body, NULL, err, errlen);
    if (!resp)
        return -1;
    feed_sections(f, jpath(resp, "contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents"));
    cJSON_Delete(resp);
    return feed_count(f);
}

int yt_feed_more(yt_feed *f, char *err, size_t errlen)
{
    cJSON *body, *resp;
    const char *client_key;
    int before = feed_count(f);
    if (!f->continuation)
        return 0;
    client_key = jstr(g_rules, "search.client");
    body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "continuation", f->continuation);
    resp = innertube("search", client_key ? client_key : "web", body, NULL, err, errlen);
    if (!resp)
        return -1;
    feed_sections(f, jpath(resp, "onResponseReceivedCommands.0.appendContinuationItemsAction.continuationItems"));
    cJSON_Delete(resp);
    return feed_count(f) - before;
}

int yt_feed_parse_json(yt_feed *f, const char *json, int continuation_page)
{
    cJSON *resp = cJSON_Parse(json);
    if (!resp)
        return -1;
    if (continuation_page)
        feed_sections(f, jpath(resp, "onResponseReceivedCommands.0.appendContinuationItemsAction.continuationItems"));
    else
        feed_sections(f, jpath(resp, "contents.twoColumnSearchResultsRenderer.primaryContents.sectionListRenderer.contents"));
    cJSON_Delete(resp);
    return 0;
}

/* ---- player ------------------------------------------------------------------ */

static void parse_mime(const char *mt, yt_format *f)
{
    const char *q;
    size_t n = strcspn(mt, ";");
    if (n >= sizeof f->mime)
        n = sizeof f->mime - 1;
    memcpy(f->mime, mt, n);
    f->mime[n] = 0;
    q = strstr(mt, "codecs=\"");
    if (q) {
        q += 8;
        n = strcspn(q, "\"");
        if (n >= sizeof f->codecs)
            n = sizeof f->codecs - 1;
        memcpy(f->codecs, q, n);
        f->codecs[n] = 0;
    }
}

static void collect_formats(const cJSON *arr, int muxed, yt_video *v)
{
    const cJSON *it;
    int count = cJSON_GetArraySize(arr);
    yt_format *nf;

    if (count <= 0)
        return;
    nf = realloc(v->formats, sizeof(yt_format) * (size_t)(v->nformats + count));
    if (!nf)
        return;
    v->formats = nf;
    cJSON_ArrayForEach(it, arr) {
        yt_format *f = &v->formats[v->nformats];
        const char *url = jstr(it, "url");
        const char *mt = jstr(it, "mimeType");
        memset(f, 0, sizeof *f);
        f->itag = (int)jnum(it, "itag", 0);
        if (mt)
            parse_mime(mt, f);
        f->width = (int)jnum(it, "width", 0);
        f->height = (int)jnum(it, "height", 0);
        f->fps = (int)jnum(it, "fps", 0);
        f->bitrate = (int)jnum(it, "bitrate", 0);
        f->content_length = (long long)jnum(it, "contentLength", -1);
        f->has_video = strncmp(f->mime, "video/", 6) == 0;
        f->has_audio = strncmp(f->mime, "audio/", 6) == 0 || (muxed && f->has_video);
        f->init_end = (long long)jnum(it, "initRange.end", -1);
        f->index_start = (long long)jnum(it, "indexRange.start", -1);
        f->index_end = (long long)jnum(it, "indexRange.end", -1);
        copy_str(f->audio_id, sizeof f->audio_id, jstr(it, "audioTrack.id"));
        copy_str(f->audio_name, sizeof f->audio_name, jstr(it, "audioTrack.displayName"));
        f->audio_default = cJSON_IsTrue(jpath(it, "audioTrack.audioIsDefault"));
        f->is_drc = cJSON_IsTrue(jpath(it, "isDrc"));
        if (url) {
            f->url = strdup(url);
        } else if (jstr(it, "signatureCipher") || jstr(it, "cipher")) {
            f->ciphered = 1;
        } else {
            continue; /* no URL at all (e.g. SABR-only) */
        }
        v->nformats++;
    }
}

static int usable_count(const yt_video *v)
{
    int i, n = 0;
    for (i = 0; i < v->nformats; i++)
        if (v->formats[i].url)
            n++;
    return n;
}

static int try_client(const char *video_id, const char *client_key, yt_video *v, char *err, size_t errlen)
{
    cJSON *body, *pc, *cpc, *resp;
    const cJSON *client = rules_client(client_key);
    const char *s;

    memset(v, 0, sizeof *v);
    copy_str(v->video_id, sizeof v->video_id, video_id);
    copy_str(v->client, sizeof v->client, client_key);
    if (!client) {
        snprintf(err, errlen, "unknown client %s", client_key);
        return -1;
    }
    copy_str(v->user_agent, sizeof v->user_agent, jstr(client, "userAgent"));

    body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "videoId", video_id);
    pc = cJSON_AddObjectToObject(body, "playbackContext");
    cpc = cJSON_AddObjectToObject(pc, "contentPlaybackContext");
    cJSON_AddStringToObject(cpc, "html5Preference", "HTML5_PREF_WANTS");
    cJSON_AddBoolToObject(body, "contentCheckOk", 1);
    cJSON_AddBoolToObject(body, "racyCheckOk", 1);
    resp = innertube("player", client_key, body, NULL, err, errlen);
    if (!resp)
        return -1;

    copy_str(v->status, sizeof v->status, jstr(resp, "playabilityStatus.status"));
    copy_str(v->reason, sizeof v->reason, jstr(resp, "playabilityStatus.reason"));
    copy_str(v->title, sizeof v->title, jstr(resp, "videoDetails.title"));
    copy_str(v->channel, sizeof v->channel, jstr(resp, "videoDetails.author"));
    v->length_seconds = (int)jnum(resp, "videoDetails.lengthSeconds", 0);
    collect_formats(jpath(resp, "streamingData.formats"), 1, v);
    collect_formats(jpath(resp, "streamingData.adaptiveFormats"), 0, v);
    {
        const cJSON *tracks = jpath(resp, "captions.playerCaptionsTracklistRenderer.captionTracks"), *t;
        int count = cJSON_GetArraySize(tracks);
        if (count > 0 && (v->captions = calloc((size_t)count, sizeof *v->captions)) != NULL) {
            cJSON_ArrayForEach(t, tracks) {
                yt_caption_track *c = &v->captions[v->ncaptions];
                const char *url = jstr(t, "baseUrl"), *kind = jstr(t, "kind");
                if (!url)
                    continue;
                copy_str(c->lang, sizeof c->lang, jstr(t, "languageCode"));
                jtext(jpath(t, "name"), c->name, sizeof c->name);
                c->asr = kind && strcmp(kind, "asr") == 0;
                c->url = strdup(url);
                v->ncaptions++;
            }
        }
    }
    if ((s = jstr(resp, "streamingData.hlsManifestUrl")))
        v->hls_url = strdup(s);
    if (jstr(resp, "streamingData.serverAbrStreamingUrl") && usable_count(v) == 0)
        v->sabr_only = 1;
    cJSON_Delete(resp);

    if (strcmp(v->status, "OK") != 0) {
        snprintf(err, errlen, "%s%s%s", v->status[0] ? v->status : "no status", v->reason[0] ? " - " : "",
                 v->reason);
        return -1;
    }
    if (usable_count(v) == 0) {
        snprintf(err, errlen, "%s",
                 v->sabr_only ? "only SABR streams returned (unsupported)"
                 : v->nformats ? "all formats need signature solving (JS), not implemented yet"
                               : "no formats returned");
        return -1;
    }
    return 0;
}

int yt_get_video(const char *video_id, yt_video *out, char *err, size_t errlen)
{
    const cJSON *clients, *c;
    char last[512] = "";
    char attempts[1024] = "";

    if (ensure_rules(err, errlen) != 0)
        return -1;
    clients = jpath(g_rules, "player_clients");
    cJSON_ArrayForEach(c, clients) {
        const cJSON *cl;
        if (!cJSON_IsString(c))
            continue;
        cl = rules_client(c->valuestring);
        if (cl && cJSON_IsTrue(jpath(cl, "requiresJs"))) {
            size_t l = strlen(attempts);
            snprintf(attempts + l, sizeof attempts - l, "%s%s: skipped (needs JS solver)", l ? "; " : "",
                     c->valuestring);
            continue;
        }
        if (try_client(video_id, c->valuestring, out, last, sizeof last) == 0)
            return 0;
        {
            size_t l = strlen(attempts);
            snprintf(attempts + l, sizeof attempts - l, "%s%s: %s", l ? "; " : "", c->valuestring, last);
        }
        yt_video_free(out);
    }
    snprintf(err, errlen, "no client worked (%s)", attempts[0] ? attempts : "no player_clients in rules");
    return -1;
}

void yt_video_free(yt_video *v)
{
    int i;
    for (i = 0; i < v->nformats; i++)
        free(v->formats[i].url);
    free(v->formats);
    free(v->hls_url);
    for (i = 0; i < v->ncaptions; i++)
        free(v->captions[i].url);
    free(v->captions);
    memset(v, 0, sizeof *v);
}

/* ---- format selection ----------------------------------------------------------- */

/* How much we want an audio track: English first (as asked), then the original language,
 * then YouTube's default; never the dynamic-range-compressed copies or audio description. */
static int track_score(const yt_format *f, const char *want)
{
    char lower[64];
    size_t i;
    int score = 0;
    if (want && want[0])
        return strcmp(f->audio_id, want) == 0 ? 100 : 0;
    for (i = 0; f->audio_name[i] && i + 1 < sizeof lower; i++)
        lower[i] = (char)tolower((unsigned char)f->audio_name[i]);
    lower[i] = 0;
    if (strncmp(f->audio_id, "en", 2) == 0 && (f->audio_id[2] == '.' || f->audio_id[2] == '-' || !f->audio_id[2]))
        score += 8;
    if (strstr(lower, "original"))
        score += 4;
    if (f->audio_default)
        score += 2;
    if (strstr(lower, "descri"))
        score -= 20;
    if (f->is_drc)
        score -= 10;
    return score;
}

static int short_side(const yt_format *f) { return f->width && f->width < f->height ? f->width : f->height; }

int yt_pick_formats2(const yt_video *v, int max_height, const char *audio_id, const yt_format **video,
                     const yt_format **audio)
{
    const yt_format *bv = NULL, *ba = NULL, *bm = NULL;
    int i, ba_score = -1000;

    for (i = 0; i < v->nformats; i++) {
        const yt_format *f = &v->formats[i];
        if (!f->url)
            continue;
        if (f->has_video && strncmp(f->codecs, "avc1", 4) == 0 && short_side(f) > 0 &&
            short_side(f) <= max_height && (f->fps == 0 || f->fps <= 30)) {
            const yt_format **slot = f->has_audio ? &bm : &bv;
            if (!*slot || short_side(f) > short_side(*slot) ||
                (short_side(f) == short_side(*slot) && f->bitrate < (*slot)->bitrate))
                *slot = f;
        } else if (!f->has_video && f->has_audio && strncmp(f->codecs, "mp4a", 4) == 0) {
            /* Within the chosen track prefer AAC-LC (mp4a.40.2): HE-AAC needs extra SBR
             * decoding, which costs more CPU on the Wii. Then the lower bitrate. */
            int score = track_score(f, audio_id) * 4 + (strcmp(f->codecs, "mp4a.40.2") == 0 ? 2 : 0);
            if (!ba || score > ba_score || (score == ba_score && f->bitrate < ba->bitrate)) {
                ba = f;
                ba_score = score;
            }
        }
    }
    /* A single muxed stream is simpler to play; use it if it is at least as tall. */
    if (bm && (!bv || !ba || short_side(bm) >= short_side(bv))) {
        *video = bm;
        *audio = NULL;
        return 0;
    }
    if (bv && ba) {
        *video = bv;
        *audio = ba;
        return 0;
    }
    return -1;
}

int yt_pick_formats(const yt_video *v, int max_height, const yt_format **video, const yt_format **audio)
{
    return yt_pick_formats2(v, max_height, NULL, video, audio);
}

int yt_audio_tracks(const yt_video *v, const yt_format **out, int max)
{
    int i, j, n = 0;
    for (i = 0; i < v->nformats && n < max; i++) {
        const yt_format *f = &v->formats[i];
        int dup = 0;
        if (!f->url || f->has_video || !f->audio_id[0] || strncmp(f->codecs, "mp4a", 4) != 0 || f->is_drc)
            continue;
        for (j = 0; j < n; j++)
            if (strcmp(out[j]->audio_id, f->audio_id) == 0)
                dup = 1;
        if (!dup)
            out[n++] = f;
    }
    return n;
}

/* ---- seek index (sidx) --------------------------------------------------------------- */

typedef struct {
    unsigned char *buf;
    size_t len, cap;
} membuf;

static int membuf_cb(void *ud, const unsigned char *data, size_t len)
{
    membuf *m = ud;
    if (m->len + len > m->cap)
        return -1;
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    return 0;
}

int yt_fetch_index(const yt_video *v, const yt_format *f, yt_index *idx, char *err, size_t errlen)
{
    membuf m;
    int rc;
    memset(idx, 0, sizeof *idx);
    if (!f->url || f->index_start < 0 || f->index_end <= f->index_start) {
        snprintf(err, errlen, "format %d has no seek index", f->itag);
        return -1;
    }
    m.cap = (size_t)(f->index_end - f->index_start + 1);
    if (m.cap > 1u << 20) {
        snprintf(err, errlen, "seek index too large");
        return -1;
    }
    m.buf = malloc(m.cap);
    m.len = 0;
    if (!m.buf)
        return -1;
    rc = yt_download_span(f->url, f->index_start, f->index_end + 1, v->user_agent, membuf_cb, &m, err, errlen);
    if (rc == 0 && yt_parse_sidx(m.buf, m.len, f->index_end + 1, idx) != 0) {
        snprintf(err, errlen, "could not read the seek index");
        rc = -1;
    }
    free(m.buf);
    return rc;
}

/* ---- download --------------------------------------------------------------------- */

int yt_download(const yt_video *v, const yt_format *f, http_body_cb cb, void *ud, char *err, size_t errlen)
{
    if (!f->url) {
        snprintf(err, errlen, "format %d has no direct URL", f->itag);
        return -1;
    }
    return yt_download_url(f->url, f->content_length, v->user_agent, cb, ud, err, errlen);
}

int yt_download_url(const char *furl, long long total, const char *user_agent, http_body_cb cb, void *ud,
                    char *err, size_t errlen)
{
    return yt_download_span(furl, 0, total, user_agent, cb, ud, err, errlen);
}

int yt_download_span(const char *furl, long long start, long long end, const char *user_agent, http_body_cb cb,
                     void *ud, char *err, size_t errlen)
{
    char headers[512];

    snprintf(headers, sizeof headers, "User-Agent: %s\r\n",
             user_agent && user_agent[0] ? user_agent : "Mozilla/5.0");
    do {
        http_request req;
        http_response resp;
        char *url;
        size_t ulen = strlen(furl) + 64;
        int rc;

        url = malloc(ulen);
        if (!url)
            return -1;
        if (end > 0) {
            long long last = start + YT_RANGE_CHUNK - 1;
            if (last >= end)
                last = end - 1;
            snprintf(url, ulen, "%s&range=%lld-%lld", furl, start, last);
            start = last + 1;
        } else {
            snprintf(url, ulen, "%s", furl);
        }
        memset(&req, 0, sizeof req);
        req.url = url;
        req.headers = headers;
        req.on_body = cb;
        req.ud = ud;
        rc = http_do(&req, &resp, err, errlen);
        free(url);
        if (rc != 0)
            return -1;
        if (resp.status != 200 && resp.status != 206) {
            snprintf(err, errlen, "stream request failed: HTTP %d%s", resp.status,
                     resp.status == 403 ? " (YouTube refused: bot check, expired URL or blocked IP)" : "");
            http_response_free(&resp);
            return -1;
        }
        http_response_free(&resp);
    } while (end > 0 && start < end);
    return 0;
}

/* ---- video IDs ----------------------------------------------------------------------- */

static int is_id_char(char c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; }

static int take_id(const char *p, char out[16])
{
    int i;
    for (i = 0; i < 11; i++)
        if (!is_id_char(p[i]))
            return -1;
    if (is_id_char(p[11]))
        return -1;
    memcpy(out, p, 11);
    out[11] = 0;
    return 0;
}

int yt_parse_video_id(const char *in, char out[16])
{
    static const char *markers[] = { "v=", "youtu.be/", "/shorts/", "/embed/", "/live/", NULL };
    int i;
    if (take_id(in, out) == 0)
        return 0;
    for (i = 0; markers[i]; i++) {
        const char *p = strstr(in, markers[i]);
        if (p && take_id(p + strlen(markers[i]), out) == 0)
            return 0;
    }
    return -1;
}
