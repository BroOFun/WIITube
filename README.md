# WIITube

A standalone YouTube client for the Nintendo Wii (and Wii U vWii) Homebrew Channel. Work in progress.

The Wii talks to YouTube directly with no proxy server, and plays low-resolution H.264
video (144p–360p), which the Wii's CPU can decode.

## Installing on the Wii / Wii U (vWii)
1. Download [WIITube-wii.zip](https://github.com/BroOFun/wiitube/raw/ci-build/WIITube-wii.zip)
   (built automatically from the latest code).
2. Unzip it onto your SD card so you get `apps/wiitube/boot.dol`.
3. Put the SD card in the Wii and open WIITube from the Homebrew Channel.

Controls (Wii Remote held upright): D-Pad moves, A selects, B goes back / deletes,
+ searches, 1 changes quality (144p/240p/360p), HOME exits. While a video plays: A pauses,
+/- change the volume, B returns to the results.

If something goes wrong, the app writes `sd:/apps/wiitube/wiitube.log`; please include it
when reporting the problem.

## Status
- Desktop prototype (`wiitube-cli`): HTTPS client, YouTube search, stream lookup and
  download, tested daily against the live site on GitHub Actions.
- Wii app: first version (v0.1), not yet tested on real hardware.

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

## Licenses
cJSON (MIT) is vendored in `third_party/`. mbedTLS (Apache-2.0) is downloaded by `deps/fetch.sh`.
