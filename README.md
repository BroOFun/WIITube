# WIITube

A standalone YouTube client for the Nintendo Wii (and Wii U vWii) Homebrew Channel. Work in progress.

The Wii talks to YouTube directly with no proxy server, and plays low-resolution H.264
video (144p–360p), which the Wii's CPU can decode.

## Status
- Desktop prototype (`wiitube-cli`) of the portable core: HTTPS client, YouTube search,
  stream lookup and download. The HTTPS layer is tested; the YouTube part still needs
  testing against the live site.
- Wii app: not started yet.

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
