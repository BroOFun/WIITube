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

Controls (Wii Remote; a Nunchuk or GameCube controller also works):

| Where | Control |
|---|---|
| Browsing | Point or D-Pad to move, **A** select, **B** back to Home, **+** search, **1** default quality, **HOME** exit |
| | Left rail: **Home** / **Shorts** / **Search**; the WiiTube logo also goes Home |
| | Scrollbar on the right: point, hold **A** and drag (or Nunchuk stick); more videos load as you scroll |
| Video | **A** pause, **Left/Right** or Nunchuk stick skip 10 s, point at the progress bar + **A** to jump |
| | **1** captions on/off (English), **2** settings: quality, captions, audio track (dubs) |
| | **+/-** or **Up/Down** volume, **B** back |
| Shorts | **Up/Down** previous / next Short (they loop like on YouTube) |

Quality (144p-480p, 480p by default) and captions on/off are remembered on the SD card.

If something goes wrong, the app writes `sd:/apps/wiitube/wiitube.log`; please include it
when reporting the problem.

## Status
- Desktop prototype (`wiitube-cli`): HTTPS client, YouTube search, stream lookup and
  download, tested daily against the live site on GitHub Actions.
- Wii app (v0.5): YouTube-style interface tested on a Wii U (vWii): Home with topic chips
  and a Shorts shelf, a Shorts page, endless results, QWERTY search keyboard; player with
  seeking, captions, quality and audio-track menu, and a vertical Shorts player. The GPU
  converts the video colours, so 480p plays smoothly.

## Building the desktop prototype (Linux / WSL / macOS)
Needs a C compiler, `make`, `curl`, and zlib headers (`zlib1g-dev` on Debian/Ubuntu).
```
sh deps/fetch.sh
make
./wiitube-cli search wii sports
./wiitube-cli info <video id or URL>
./wiitube-cli get <video id or URL> -q 240
./wiitube-cli seek <video id or URL> 60      # download from 1:00 the way the Wii seeks
./wiitube-cli captions <video id or URL> 20  # English caption at 0:20
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
`python3 tools/make_logo.py` draws the logo from shapes (in the style of the original
artwork, `assets/logo_source.webp`) and writes the transparent `assets/logo.png`, the
Homebrew Channel icon `wii/icon.png` (128x48) and the in-app images in
`src/platform/wii/logo_data.c`.

## Licenses
cJSON (MIT) and stb_image (public domain / MIT) are vendored in `third_party/`. mbedTLS
(Apache-2.0) is downloaded by `deps/fetch.sh`. The UI font is Roboto (Apache-2.0,
`assets/fonts/`).
