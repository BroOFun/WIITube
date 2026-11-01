#!/bin/sh
# Downloads pinned third-party sources into deps/src/ (git-ignored).
# Usage: sh deps/fetch.sh
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/src"
mkdir -p "$SRC"

MBEDTLS_VER=3.6.5
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VER/mbedtls-$MBEDTLS_VER.tar.bz2"
# Recorded on first download (2026-11-01); not yet cross-checked against the official release notes. Update together with MBEDTLS_VER.
MBEDTLS_SHA256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8

fetch() { # url out
    if command -v curl >/dev/null 2>&1; then curl -sSfL -o "$2" "$1"; else wget -q -O "$2" "$1"; fi
}

if [ ! -f "$SRC/mbedtls/include/mbedtls/ssl.h" ]; then
    tarball="$SRC/mbedtls-$MBEDTLS_VER.tar.bz2"
    echo "fetching mbedtls $MBEDTLS_VER"
    fetch "$MBEDTLS_URL" "$tarball"
    sum=$(sha256sum "$tarball" | cut -d' ' -f1)
    if [ "$MBEDTLS_SHA256" != "__FILL_ME__" ] && [ "$sum" != "$MBEDTLS_SHA256" ]; then
        echo "mbedtls checksum mismatch: $sum" >&2; exit 1
    fi
    rm -rf "$SRC/mbedtls" "$SRC/mbedtls-$MBEDTLS_VER"
    tar -xjf "$tarball" -C "$SRC"
    mv "$SRC/mbedtls-$MBEDTLS_VER" "$SRC/mbedtls"
    rm -f "$tarball"
fi
echo "deps ready in $SRC"
