# WIITube: project notes for Claude sessions

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
  - `wiitube_mbedtls_user.h`: mbedTLS config tweaks (entropy via `net_entropy`).
- `src/platform/linux/`: Linux `net.h`. `src/platform/wii/`: TODO (libogc `net_*`).
- `src/cli/main.c`: `wiitube-cli` test harness (`fetch`, `search`, `info`, `get`).
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

## Status (2026-11-01)
- Done: HTTPS stack verified (TLS + certificate checks, gzip, chunked, 5 MB download
  byte-exact, ASan/UBSan clean); YouTube search/player/format/download code written.
- **NOT yet tested against YouTube**: the session's network policy blocked
  `www.youtube.com`, `*.googlevideo.com`, `i.ytimg.com`. Also, YouTube often blocks cloud
  IPs, so the real test may need the owner's home connection.
- devkitPro package hosts (`pkg.devkitpro.org`) and `dl.dolphin-emu.org` were blocked too,
  so no Wii toolchain or Dolphin yet.

## Roadmap
1. Verify search/info/get against real YouTube (needs network access).
2. Wii port: `Makefile.wii` (devkitPPC/libogc), `src/platform/wii/net_wii.c`, SD-card CA
   bundle, entropy source, fit in 88 MB RAM.
3. Player: FFmpeg H.264 + AAC decode (WiiMC proved FFmpeg runs on Wii), YUV→RGB and
   scaling on the GPU (GX/TEV), audio via ASND/AESND, A/V sync, two streams (video + audio).
   Quality settings: max height (144/240/360), skip-deblocking fast mode.
4. UI: on-screen keyboard search, results list with small thumbnails, Wii Remote controls.
5. Rules auto-update on startup (+ signature check before adding any downloaded scripts).
6. JS fallback (QuickJS + EJS solver) for when visionos stops working.
7. Scheduled check that runs `info`/`get` and reports breakage.
