<p align="center"><img src="assets/logo.png" alt="WiiTube" width="360"></p>

# WiiTube

A standalone YouTube client for the Nintendo Wii (and Wii U vWii) Homebrew Channel. Work in progress.

The Wii talks to YouTube directly with no proxy server, and plays low-resolution H.264
video (up to 480p): the CPU decodes, the GPU converts colours and scales.

## Installing on the Wii / Wii U (vWii)
1. Download [WiiTube.zip](https://github.com/BroOFun/wiitube/raw/ci-build/WiiTube.zip)
   (built automatically from the latest code).
2. Unzip it onto your SD card so you get `apps/wiitube/boot.dol`.
3. Put the SD card in the Wii (on a Wii U: open **Wii Menu** first) and start WiiTube from
   the Homebrew Channel.

Controls: point with the Wii Remote (or use the D-Pad), A selects, B goes back, + opens
search, 1 changes quality (480p by default; 144p/240p/360p/480p, remembered on the SD card),
HOME exits.
While a video plays: A pauses, +/- change the volume, B returns to the videos.

If something goes wrong, the app writes `sd:/apps/wiitube/wiitube.log`; please include it
when reporting the problem.

## Status
- Desktop prototype (`wiitube-cli`): HTTPS client, YouTube search, stream lookup and
  download, tested daily against the live site on GitHub Actions.
- Wii app (v0.4): YouTube-style interface (thumbnail grid, topic chips, search keyboard,
  player controls) tested on a Wii U (vWii). v0.4 moves the video colour conversion to the
  Wii's GPU so 480p plays smoothly; 480p is the default.

## Building the desktop prototype (Linux / WSL / macOS)
Needs a C compiler, `make`, `curl`, and zlib headers (`zlib1g-dev` on Debian/Ubuntu).
```
sh deps/fetch.sh
make
./wiitube-cli search wii sports
./wiitube-cli info <video id or URL>
./wiitube-cli get <video id or URL> -q 240
```
`get` saves the video and audio as separate files; to check them on a PC:
`ffmpeg -i ID.video.mp4 -i ID.audio.m4a -c copy test.mp4`.

## Keeping it working
YouTube changes things often. The client identities live in `rules/youtube.json`,
which tracks [yt-dlp](https://github.com/yt-dlp/yt-dlp)'s YouTube extractor. Most
fixes are edits to that file, and the Wii app will download the latest version at startup.

## UI previews
`make ui-preview` renders every screen to PNG in `build/linux/preview/` with a software
renderer, so the Wii interface can be checked on a PC (needs Pillow and the ffmpeg CLI).

## Logo
`assets/logo_source.webp` is the original artwork; `python3 tools/make_logo.py
assets/logo_source.webp` regenerates the transparent `assets/logo.png`, the Homebrew
Channel icon `wii/icon.png` (128x48) and the in-app images in
`src/platform/wii/logo_data.c`.

## Licenses
cJSON (MIT) and stb_image (public domain / MIT) are vendored in `third_party/`. mbedTLS
(Apache-2.0) is downloaded by `deps/fetch.sh`. The UI font is Roboto (Apache-2.0,
`assets/fonts/`).
