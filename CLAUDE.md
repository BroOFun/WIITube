# WiiTube: project notes for Claude sessions

The app is called **WiiTube** (capital W, lowercase ii); the GitHub repo is BroOFun/wiitube.

Goal: a **standalone** YouTube client for the Wii (runs on the owner's Wii U vWii with the
Homebrew Channel). No proxy server: the Wii itself talks to YouTube over HTTPS and plays
low-resolution H.264 (144p–360p, ≤30fps). Read this file fully before working.

## Architecture
- `src/core/`: portable C99, shared by the Linux CLI and the future Wii app.
  - `http.c`: HTTP/1.1 over TLS (mbedTLS 3.6), gzip (zlib), chunked, redirects,
    streaming callbacks. CONNECT-proxy support exists only for dev containers.
  - `yt.c`: innertube API (`/youtubei/v1/search`, `/youtubei/v1/player`), format
    picking (H.264 `avc1` ≤ height + AAC `mp4a`, or muxed), downloads using
    `&range=` 10 MiB chunks (same as yt-dlp).
  - `net.h`: platform layer (sockets, entropy, CA bundle path, proxy).
  - `wiitube_mbedtls_user.h`: mbedTLS config tweaks (entropy via `plat_entropy`).
  - `ring.c` (blocking byte buffer), `stream.c` (download thread -> ring), `media.c` (FFmpeg
    demux/decode of non-seekable fMP4 from a ring), `convert.c` (YUV420 -> GX tiled RGB565),
    `vqueue.c` (decode thread + 6 planar texture slots + adaptive frame skipping), `thread.h`, `log.c`,
    `feed.c` (growing result lists), `seekidx.c` (sidx parser), `captions.c` (json3 captions),
    `thumbs.c` / `image.c` (thumbnail cache, JPEG decode), `yuv_tev.c` (GPU colour model).
- `src/platform/linux/`: desktop `net.h` + pthreads.
- `src/platform/wii/`: Wii app. `main.c` (startup, input, browse loop, page loading),
  `player.c` (playback, seeking, captions, Shorts), `gfx.c`
  (console menus + GX video quad + progress bar), `audio.c` (ASND double-buffered voice,
  lock-free ring, clock from block start ticks), `net_wii.c` (if_config, net_*, 8 KiB I/O cap,
  IOS-timing entropy, built-in CA), `thread_wii.c` (LWP + mbedTLS THREADING_ALT).
  Platform functions are named `plat_*` because libogc already owns `net_*`.
- `src/cli/main.c`: `wiitube-cli` test harness (`fetch`, `search`, `info`, `get`, `seek`,
  `captions`, `dump`).
- `rules/youtube.json`: **client identities and endpoints, kept out of the C code** so
  fixes are mostly data changes. Embedded at build time as the fallback; the Wii app
  will download the latest copy from this repo
  (`https://raw.githubusercontent.com/BroOFun/wiitube/main/rules/youtube.json`).
- `deps/fetch.sh`: downloads pinned mbedTLS into `deps/src/` (git-ignored).
- `third_party/cJSON`: vendored cJSON 1.7.19 (MIT).

## Build / test (Linux)
```
sh deps/fetch.sh && make
./wiitube-cli fetch https://example.com/
./wiitube-cli search wii sports
./wiitube-cli info dQw4w9WgXcQ
./wiitube-cli get dQw4w9WgXcQ -q 240     # writes <id>.video.mp4 + <id>.audio.m4a
```
Sanitizer build: `make clean && make CFLAGS="-O1 -g -fsanitize=address,undefined" LIBS="-lz -fsanitize=address,undefined"`.
Player pipeline test (needs `libavformat-dev libavcodec-dev libavutil-dev` + ffmpeg CLI):
`make test-media` (YouTube-shaped clips through ring -> FFmpeg -> conversion -> vqueue).

## Wii build
Only in CI: `.github/workflows/wii-build.yml` runs in the `devkitpro/devkitppc` container,
builds FFmpeg (`deps/build_ffmpeg_wii.sh`, cached) and `make -f Makefile.wii`, then
force-pushes `status.txt`, `build.log`, `ffmpeg.log`, `diagnostics.txt` and
`WiiTube.zip` to the **`ci-build`** branch
(`git fetch origin ci-build && git show FETCH_HEAD:status.txt`). Users download
`https://github.com/BroOFun/wiitube/raw/ci-build/WiiTube.zip`.
Running downloaded toolchains in the dev container is blocked by the session's safety
policy, so syntax-check Wii code locally against cloned libogc headers instead:
`gcc -fsyntax-only -std=gnu99 -DGEKKO -DHW_RVL -DWIITUBE_PLATFORM_WII ... -I<libogc>/gc`.
On the Wii the app logs to `sd:/apps/wiitube/wiitube.log` — ask the user for it.

## Live test on GitHub Actions
`.github/workflows/youtube-test.yml` runs `tests/youtube_smoke.sh` (search, info, 240p get,
ffprobe check) on pushes touching code, daily at 09:17 UTC, and on demand. It force-pushes
`results.txt` to the **`ci-results`** branch, so a session that can't use the Actions API can
read results with `git fetch origin ci-results && git show origin/ci-results:results.txt`.
The downloaded video is uploaded as the `test-video` artifact. Runner IPs are cloud IPs, so
"Sign in to confirm you're not a bot" there doesn't necessarily mean home connections fail.

## How YouTube access works (as of yt-dlp 2026.10.26)
- Search uses the `web` client (no tokens needed).
- Player: `visionos` client (`VISIONOS` 1.02, client id 101) needs **no JavaScript and no
  PO token**, so it is first. It is yt-dlp's default JS-less client. "Made for kids"
  videos don't work with it.
- Fallback `tv_samsung` (`TVHTML5` 5.x) needs no PO token but **does** need the JS player
  (signature + `n` solving). Not implemented yet; plan: embed QuickJS and run yt-dlp's
  EJS solver (`yt_dlp_ejs/yt/solver/*.js`, from the `yt-dlp-ejs` PyPI package), caching
  per player version on the SD card.
- Web/mweb/android/ios clients need PO tokens (BotGuard): not feasible on a Wii.

## When YouTube breaks it (maintenance playbook)
1. Get the newest yt-dlp (`pip download --no-deps yt-dlp`) and compare
   `yt_dlp/extractor/youtube/_base.py` `INNERTUBE_CLIENTS` and `_video.py`
   `_DEFAULT_CLIENTS` / `_DEFAULT_JSLESS_CLIENTS` with `rules/youtube.json`.
2. Update versions, user agents, or the `player_clients` order in `rules/youtube.json` first.
   Only change C code if the request/response *shape* changed.
3. Bump `rules_version`/`updated`, then test with `wiitube-cli info` and `get`.

## Status (2026-11-02) — v0.6.1: search fixed, the app's browse flow tested live
- User report on v0.6: "nothing is being searched, i can only watch shorts". Cause (also
  behind v0.5's "chips and Shorts don't work"): main.c called
  `yt_feed_start(&job->result, job->result.query, ...)` and yt_feed_start began with
  `yt_feed_free(f)`, which zeroed the query it was given, so every Home topic and search
  asked YouTube for "". The Shorts tab passed a separate buffer, so it worked. Fixed in
  yt_feed_start (copies the query first; an empty query is now an error).
- CI never caught it because the live tests called yt_feed_start with their own buffers.
  Now the page loader + watch history live in portable `src/app/pages.c` (Wii main.c
  calls `pages_tick`, `pages_handle`, `pages_search`, `pages_after_play`), and
  `tests/app_sim.c` runs that same code with the real browse screen and keyboard, driven
  by simulated Wii Remote input against live YouTube in `youtube-test.yml` (Home loads,
  chip change, typing "wii sports" on the keyboard + search, Shorts shelves present,
  stick scroll loads page 2, For you after a watch). Results: `app.txt` and `app-*.png` on
  `ci-results`; the workflow fails if the simulation fails. Keep new browse logic in
  src/app or src/ui so it stays covered.

## Status (2026-11-01, night) — v0.6: YouTube's own order, recommendations and Shorts feed
- User report on v0.5: "the search categories tabs and shorts don't work"; wants search
  exactly like YouTube's default (Shorts shelves among the videos), recommendations pulled
  from YouTube itself, and working Shorts.
- Search = YouTube's normal search (no filter params). `yt_feed` is now ONE ordered list
  (`items`); Shorts in a shelf carry `shelf` = shelf number (`gridShelfViewModel`,
  `reelShelfRenderer`, `richShelfRenderer` start a new number; `yt_feed_merge` renumbers
  shelves of later pages). Also parsed: `gridVideoRenderer` (titled `shelfRenderer`
  horizontal lists), `compactVideoRenderer`, full `lockupViewModel` (channel, views,
  date, duration badge). Real page order: `[Shorts x5] v v v [Shorts x20] v v v ...`.
- Recommendations, checked live with `wiitube-cli dump`: logged-out home is empty on every
  client (web: "Try searching to get started", TV/MWEB: "sign in"; FEtrending/FEexplore
  give HTTP 400), and reporting watches (videostats playback/watchtime pings, HTTP 204)
  does NOT make the logged-out home fill in. What works logged out:
  - `next` (Up next) for a video: 20 related videos + continuation token
    (`onResponseReceivedEndpoints.0.appendContinuationItemsAction`). `YT_FEED_RELATED`
    uses it: query = watched video IDs (newest first); page 1 of each seed, then the last
    seed's continuation; watched videos are dropped. This is the "For you" chip.
  - `reel/reel_watch_sequence` with `sequenceParams` = base64 of protobuf {1: videoId}
    (built by us; YouTube's own sequenceParams also list the next IDs): ~8 Shorts that
    follow that Short. Responses are ~700 KB because entry 0 carries prefetched player
    data. `YT_FEED_REELS`: query = seed, continuation = last Short (a walk along related
    Shorts). The continuation token YouTube returns gave only 1 entry, so it isn't used.
- Wii app: watch history `sd:/apps/wiitube/history.txt` ("v ID"/"s ID", newest first,
  40 max; `history_add` when a video starts). Home chips: "For you" (only once something
  was watched; refreshed whenever picked) then topics (normal search). The rail's Shorts
  button opens the Shorts player directly (`player_run` with `feed == NULL`), continuing
  from the last Short watched, else from Shorts found by searching "#shorts". A Short
  tapped in a list plays, then the rest of its shelf, then the Shorts feed after it
  (player.c keeps its own queue and `reels_job`). One page-loading job slot in main.c.
- `.github/workflows/wii-build.yml` keeps the previous WiiTube.zip on `ci-build` when a
  build fails (so the download link never breaks); `zip_commit.txt` says which commit it is.
  Don't cancel a Wii build run: the publish step runs anyway.

## Status (2026-11-01, late) — v0.5: browse/player overhaul (not yet tested on hardware)
- User asked for: home recommendations reachable while browsing, QWERTY keyboard, a sidebar
  for pointer scrolling, endless results (home + search), Shorts (Down = next), seeking
  (progress-bar click, D-pad/Nunchuk ±5-10 s), captions on a button, English by default,
  choosing auto-dubbed audio tracks, WiiTube logo = Home, a smoother logo, and changing the
  quality during playback. All implemented; the Wii build and live CI checks pass.
- Data (`yt.c`, `feed.c`, `seekidx.c`, `captions.c`; tested by `make test-ytdata` against real
  responses in `tests/fixtures/`, captured with `wiitube-cli dump`):
  - Endless feeds: `yt_feed_start` (search with params Videos `EgIQAQ==` / Shorts `EgIQCQ==`),
    `yt_feed_more` (continuation token). Paging runs in a job on a *separate* feed made by
    `yt_feed_cursor`; the main thread merges it with `yt_feed_merge`, so the UI never reads a
    list that another thread is changing. Shorts: `shortsLockupViewModel` (vertical
    `oar2.jpg` thumbnail URL with signed params) or `videoRenderer` with a `/shorts/` URL.
    Logged-out "home" has no recommendations, so Home = topic-chip searches.
  - Captions: `captionTracks[].baseUrl` + `&fmt=json3` (+ `&tlang=en` to translate); English
    track preferred, real over auto-generated.
  - Audio tracks: format `audioTrack` (id like `en.4`, displayName, audioIsDefault); the
    picker prefers English / "original", avoids descriptive and DRC tracks. Not yet seen
    live on CI (the dubbed test video hit a bot check); field names are from yt-dlp.
  - Seeking: each DASH format has `initRange` (ftyp+moov) and `indexRange` (sidx). The sidx
    gives subsegments (~5 s video, ~10 s audio). `yt_stream_start_at` downloads
    [0, init_end] then [offset, end): FFmpeg sees a valid fragmented file starting at a
    keyframe, with absolute times from the `tfdt` boxes (`media_expect_start` shifts them
    if a file lacks tfdt). Playback starts at the video piece's start; audio before it is
    dropped in `feed_audio`. Verified live: `wiitube-cli seek dQw4w9WgXcQ 60` -> first frame
    55.400 s exactly as the index says (smoke test step `seek-timestamps`).
- UI (`src/ui/browse.c`, `keyboard.c`, `player_ui.c`; `screens.c` is gone; v0.6 replaced the
  separate Shorts list / Shorts page described here): left rail
  Home/Shorts/Search, topic chips, video grid with a Shorts shelf after 2 rows, Shorts page
  grid, scrollbar draggable with A held (`ui_input.held`), Nunchuk stick scroll
  (`ui_input.stick_x/y`), footer spinner while the next page loads (`want_videos` /
  `want_shorts` flags tell main.c what to load). Player: back button, progress bar with
  hover time, 10 s nudges collected for 0.6 s before one seek, CC button, gear menu
  (quality / captions / audio track), notices, vertical Shorts layout with up/down buttons.
  `make ui-preview` renders all of it (incl. Shorts and menus).
- Wii app split: `main.c` (input, settings, two feed job slots, browse loop), `player.c`
  (session: start job = lookup + pick + seek index + streams + decoders; restart for
  seek/quality/audio; captions job; Shorts list navigation with loop; quality for Shorts
  capped at 360p by default since they are shown 270 px wide), `app.h` (shared).
  settings.txt now also stores `captions=0/1`.
- Thumbnails: Shorts use the normal `mqdefault.jpg` cropped to 9:16 when drawn (`draw_cover`,
  `dr_image_part`). Checked live: the vertical URLs in search results (`oardefault.jpg`/
  `oar2.jpg` with `sqp`/`rs` params) are served as **WebP** (stb_image can't decode), plain
  `oar2.jpg` is a 1080x1920 JPEG (too big for the Wii), `frame0.jpg` is a 268x480 JPEG of
  the first frame. `thumbs_get_url` (any URL, own size) stays available.
- Logo: `tools/make_logo.py` now draws it from shapes at high resolution (no source image).
- Next: user's hardware test of v0.5 (ask for wiitube.log: seek timing, Shorts, captions);
  possible follow-ups: preloading the next Short, precise seeking (decode-and-drop to the
  exact time), seeking for muxed formats.

## Status (2026-11-02) — v0.4: smooth 480p
- User report on v0.3: everything works; 480p stutters (low fps) although downloading keeps up.
- Cause: the CPU converted every frame YUV -> tiled RGB565 (~34x more work than needed; at
  480p roughly a whole frame period on Broadway), stealing time from H.264 decoding.
- Fix: the GPU converts. `vqueue` now only tiles the Y/U/V planes into I8 textures
  (`conv_plane_to_i8`, ~memcpy cost) and `dr_video` (gfx.c) draws them with a 6-stage TEV
  combiner defined in `src/core/yuv_tev.[ch]`. `yuv_tev_pixel()` is a bit-exact model of
  those stages (Dolphin's TEV integer maths) used by `make test-yuv`, `test-media` and the
  preview renderer, so the constants are tested without hardware (mean error ~0.5/255).
  Stage layout: P=C0+ky*Y; +k1*V (R); -k2*V (G); +1.0*U via vertex colour (0,0,255) (B);
  +k3*U (B); 2*(P-k5*U) clamp (G). K0 alpha = ky (KCSEL_K0_A), TEVREG0 = signed constants.
- FFmpeg: default -O3 (no -O2 override), `--disable-safe-bitstream-reader`,
  `--enable-hardcoded-tables`; video decoder uses `AV_CODEC_FLAG2_FAST`.
- 480p is the default (`DEFAULT_QUALITY`); settings.txt now has `version=2` and older files
  are ignored so existing installs switch to 480p.
- Per-video log line now includes average decode and prepare (tiling) ms per frame — ask
  the user for it if 480p still stutters; adaptive skipping (vqueue LATE_SKIP*) remains.

## Status (2026-11-01, night) — v0.3
- v0.2 on hardware: video playback works (user confirmed). v0.3 = YouTube-style UI:
  - `src/ui/` (portable): `draw.h` API (fill, gradient, atlas region, image, clip) on a
    virtual canvas `ui_width()` x 480 (854 on 16:9, 640 on 4:3); `ui_common.c` (text from the
    Roboto atlas, round rects via circle/inverse-corner atlas pieces, spinner, cursor);
    `screens.c` (browse grid + search pill + chips, keyboard, player overlay, messages).
  - Backends: `src/platform/wii/gfx.c` (GX; TEV "mask" mode = vertex colour x atlas alpha,
    "image" mode = modulate; ortho projection maps the virtual canvas to the 640 EFB) and
    `src/platform/linux/soft_draw.c` (software, for `make ui-preview` PNGs).
  - `tools/make_atlas.py` (fonts+icons -> `src/ui/atlas_data.c`), `tools/make_logo.py`.
    (v0.5 replaced `screens.c` with browse.c / keyboard.c / player_ui.c, see above.)
  - Thumbnails: `src/core/thumbs.c` (2 worker threads, LRU 48) fetch
    `i.ytimg.com/vi/<id>/mqdefault.jpg`, `image.c` decodes (stb_image) to 160x90 RGB565.
  - http.c keep-alive pool (POOL_MAX 6, 20 s idle) — big win for thumbnails.
  - main.c: background job thread for connect/search/open; player loop draws video via
    dr_image of the vqueue texture; Wii Remote IR pointer (WPAD_SetVRes = canvas size);
    quality 144/240/360/480 saved in `sd:/apps/wiitube/settings.txt`; memory estimate check
    before playback (needed_kb vs free_kb) and memory logged after each video.
- Next the user wants Shorts.

## Status (2026-11-01, evening)
- First hardware test (Wii U vWii, v0.1): network, TLS and **search work**; starting a video
  failed with "Out of memory". Cause: libogc (tuxedo) has a fixed pool of 64 LWP mutexes and
  mbedTLS THREADING_C makes one per RSA key, so parsing the ~145 CA certificates used them
  all; later wt_mutex_new() failed. Fix in v0.2: all mbedTLS mutexes share one recursive
  LWP mutex (thread_wii.c). Also LWP conds are a 64-slot pool. Watch for other pool limits.
- v0.2 adds the WiiTube logo (header + splash drawn into the console XFB by
  gfx_console_image, HBC icon.png), memory logging (log_memory) and clearer errors.

## Status (2026-11-01, later)
- **Wii app v0.1 builds** in CI (commit 15b8c3f): boot.dol 4.4 MB, zip on `ci-build`.
  Not yet run on hardware or in Dolphin. FFmpeg 7.1.2 for the Wii builds with
  `--disable-pthreads`; FFmpeg warns its libraries must then not be used from several
  threads. We keep that safe: both decoders are opened (and closed) on the main thread while
  the vqueue worker is not running, each thread only touches its own context, and av_log
  goes through the mutex-protected wlog. Keep it that way when changing playback code.
- Next: user's first hardware test (ask for `sd:/apps/wiitube/wiitube.log`), then likely
  fixes; Dolphin-in-CI smoke test; thumbnails; seeking; nicer GX menu UI.

## Status (2026-11-01)
- Done: HTTPS stack verified (TLS + certificate checks, gzip, chunked, 5 MB download
  byte-exact, ASan/UBSan clean).
- **YouTube verified end-to-end on GitHub Actions (commit 50f7524)**: search returned 20
  results; `visionos` player returned direct URLs for every format (H.264 144p–1080p, VP9,
  AV1, AAC 139/140, Opus); 240p H.264 (itag 133) + AAC downloaded; ffprobe confirmed
  h264 426x240 and aac 44.1 kHz.
- Findings: visionos returned **no muxed formats** (no itag 18), so the Wii player must play
  separate video + audio streams. The picker currently takes the lowest-bitrate AAC, which
  is itag 139 (HE-AAC, `mp4a.40.5`); AAC-LC (`mp4a.40.2`, itag 140) is cheaper to decode
  and may suit the Wii better.
- This dev container's network policy blocks YouTube, `pkg.devkitpro.org` and
  `dl.dolphin-emu.org`. Use the Actions test for live checks. A Wii build could also run in
  Actions with the `devkitpro/devkitppc` Docker image.

## Roadmap
1. ~~Verify search/info/get against real YouTube~~ (done 2026-11-01 via Actions).
2. Wii port: `Makefile.wii` (devkitPPC/libogc), `src/platform/wii/net_wii.c`, SD-card CA
   bundle, entropy source, fit in 88 MB RAM.
3. Player: FFmpeg H.264 + AAC decode (WiiMC proved FFmpeg runs on Wii), YUV→RGB and
   scaling on the GPU (GX/TEV), audio via ASND/AESND, A/V sync, two streams (video + audio).
   Quality settings: max height (144/240/360), skip-deblocking fast mode.
4. UI: on-screen keyboard search, results list with small thumbnails, Wii Remote controls.
5. Rules auto-update on startup (+ signature check before adding any downloaded scripts).
6. JS fallback (QuickJS + EJS solver) for when visionos stops working.
7. Scheduled check that runs `info`/`get` and reports breakage.
