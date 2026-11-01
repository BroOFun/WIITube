#!/bin/sh
# Cross-compiles a minimal FFmpeg for the Wii: H.264 + AAC decoders, MP4 demuxer only.
# Run inside the devkitpro/devkitppc container. Usage: sh deps/build_ffmpeg_wii.sh PREFIX
set -eu

PREFIX=$(mkdir -p "$1" && cd "$1" && pwd)
VER=7.1.2
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/src/ffmpeg-$VER"

if [ -f "$PREFIX/lib/libavcodec.a" ]; then
    echo "FFmpeg already built in $PREFIX"
    exit 0
fi
: "${DEVKITPRO:?DEVKITPRO not set}"
: "${DEVKITPPC:?DEVKITPPC not set}"
export PATH="$DEVKITPPC/bin:$PATH"

if [ ! -d "$SRC" ]; then
    mkdir -p "$HERE/src"
    curl -sSfL -o "$HERE/src/ffmpeg.tar.xz" "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz"
    tar -xJf "$HERE/src/ffmpeg.tar.xz" -C "$HERE/src"
    rm -f "$HERE/src/ffmpeg.tar.xz"
fi

MACHDEP="-DGEKKO -mrvl -mcpu=750 -meabi -mhard-float"
cd "$SRC"
./configure \
    --prefix="$PREFIX" \
    --enable-cross-compile --cross-prefix=powerpc-eabi- \
    --arch=ppc --cpu=750 --target-os=none \
    --extra-cflags="$MACHDEP -O2 -I$DEVKITPRO/libogc/include" \
    --extra-ldflags="$MACHDEP -L$DEVKITPRO/libogc/lib/wii" \
    --extra-libs="-logc -lm" \
    --pkg-config=false \
    --disable-everything --disable-autodetect \
    --disable-programs --disable-doc --disable-debug --disable-network \
    --disable-pthreads --disable-w32threads --disable-os2threads \
    --disable-avdevice --disable-swscale --disable-swresample --disable-avfilter \
    --disable-asm --disable-runtime-cpudetect \
    --enable-avcodec --enable-avformat --enable-avutil \
    --enable-decoder=h264 --enable-decoder=aac \
    --enable-parser=h264 --enable-parser=aac \
    --enable-demuxer=mov \
    || { echo "configure failed; tail of ffbuild/config.log:"; tail -60 ffbuild/config.log; exit 1; }
make -j"$(nproc)"
make install
echo "FFmpeg $VER installed to $PREFIX"
