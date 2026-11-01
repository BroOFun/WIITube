#include "captions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http.h"

static int is_english(const char *lang) { return strncmp(lang, "en", 2) == 0 && (lang[2] == 0 || lang[2] == '-'); }

int cc_pick_english(const yt_video *v, int *translate)
{
    int i, asr = -1;
    *translate = -1;
    for (i = 0; i < v->ncaptions; i++) {
        if (is_english(v->captions[i].lang)) {
            if (!v->captions[i].asr)
                return i;
            if (asr < 0)
                asr = i;
        }
    }
    if (asr >= 0)
        return asr;
    if (v->ncaptions > 0)
        *translate = 0; /* the first track, translated by YouTube */
    return -1;
}

static int cmp_cue(const void *a, const void *b)
{
    return ((const cc_cue *)a)->start_ms - ((const cc_cue *)b)->start_ms;
}

int cc_parse_json3(const char *json, cc_track *out)
{
    cJSON *root = cJSON_Parse(json), *events, *ev;
    int cap;
    out->cues = NULL;
    out->n = 0;
    if (!root)
        return -1;
    events = cJSON_GetObjectItem(root, "events");
    cap = cJSON_GetArraySize(events);
    if (cap > 0)
        out->cues = calloc((size_t)cap, sizeof *out->cues);
    cJSON_ArrayForEach(ev, events) {
        cJSON *segs = cJSON_GetObjectItem(ev, "segs"), *seg;
        cJSON *t0 = cJSON_GetObjectItem(ev, "tStartMs"), *dur = cJSON_GetObjectItem(ev, "dDurationMs");
        size_t len = 0;
        char *text;
        if (!segs || !cJSON_IsNumber(t0) || !out->cues)
            continue;
        cJSON_ArrayForEach(seg, segs) {
            cJSON *u = cJSON_GetObjectItem(seg, "utf8");
            if (cJSON_IsString(u))
                len += strlen(u->valuestring);
        }
        text = malloc(len + 1);
        if (!text)
            continue;
        text[0] = 0;
        cJSON_ArrayForEach(seg, segs) {
            cJSON *u = cJSON_GetObjectItem(seg, "utf8");
            if (cJSON_IsString(u))
                strcat(text, u->valuestring);
        }
        /* Skip cues that are only whitespace (auto-captions use them as line breaks). */
        if (strspn(text, " \n") == strlen(text)) {
            free(text);
            continue;
        }
        out->cues[out->n].start_ms = t0->valueint;
        out->cues[out->n].end_ms = t0->valueint + (cJSON_IsNumber(dur) ? dur->valueint : 3000);
        out->cues[out->n].text = text;
        out->n++;
    }
    cJSON_Delete(root);
    qsort(out->cues, (size_t)out->n, sizeof *out->cues, cmp_cue);
    return 0;
}

int cc_fetch(const yt_video *v, int i, const char *tlang, cc_track *out, char *err, size_t errlen)
{
    http_request req;
    http_response resp;
    char *url;
    size_t ulen;
    int rc = -1;

    memset(out, 0, sizeof *out);
    if (i < 0 || i >= v->ncaptions) {
        snprintf(err, errlen, "no such caption track");
        return -1;
    }
    ulen = strlen(v->captions[i].url) + 64;
    url = malloc(ulen);
    if (!url)
        return -1;
    snprintf(url, ulen, "%s&fmt=json3%s%s", v->captions[i].url, tlang ? "&tlang=" : "", tlang ? tlang : "");
    memset(&req, 0, sizeof req);
    req.url = url;
    req.headers = "User-Agent: Mozilla/5.0\r\n";
    req.max_buffer = 4u << 20;
    if (http_do(&req, &resp, err, errlen) == 0) {
        if (resp.status == 200 && resp.body && cc_parse_json3((const char *)resp.body, out) == 0)
            rc = 0;
        else
            snprintf(err, errlen, "captions: HTTP %d", resp.status);
        http_response_free(&resp);
    }
    free(url);
    if (rc == 0)
        snprintf(out->label, sizeof out->label, "%s%s", v->captions[i].name, tlang ? " (translated)" : "");
    return rc;
}

const char *cc_text_at(const cc_track *t, double sec, char *buf, size_t buflen)
{
    const int ms = (int)(sec * 1000);
    int lo = 0, hi = t->n - 1, last = -1, prev = -1, i;
    if (!t || t->n == 0)
        return NULL;
    while (lo <= hi) { /* last cue starting at or before ms */
        int mid = (lo + hi) / 2;
        if (t->cues[mid].start_ms <= ms) {
            last = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (last < 0 || t->cues[last].end_ms <= ms)
        return NULL;
    for (i = last - 1; i >= 0 && i >= last - 3; i--)
        if (t->cues[i].end_ms > ms && !strchr(t->cues[i].text, '\n') && !strchr(t->cues[last].text, '\n')) {
            prev = i;
            break;
        }
    if (prev >= 0)
        snprintf(buf, buflen, "%s\n%s", t->cues[prev].text, t->cues[last].text);
    else
        snprintf(buf, buflen, "%s", t->cues[last].text);
    return buf;
}

void cc_free(cc_track *t)
{
    int i;
    for (i = 0; i < t->n; i++)
        free(t->cues[i].text);
    free(t->cues);
    memset(t, 0, sizeof *t);
}
