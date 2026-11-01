# Linux/desktop build of the portable core + test CLI.
#   sh deps/fetch.sh   (once, downloads mbedTLS)
#   make               (builds ./wiitube-cli)
# The Wii build will live in Makefile.wii (devkitPPC + libogc).

CC      ?= cc
BUILD   := build/linux
MBED    := deps/src/mbedtls

ifeq ($(wildcard $(MBED)/include/mbedtls/ssl.h),)
$(error mbedTLS sources missing: run "sh deps/fetch.sh" first)
endif

MBED_SRCS := $(wildcard $(MBED)/library/*.c)
MBED_OBJS := $(patsubst $(MBED)/library/%.c,$(BUILD)/mbedtls/%.o,$(MBED_SRCS))

APP_SRCS := src/core/http.c src/core/yt.c src/platform/linux/net_linux.c src/cli/main.c \
            src/platform/linux/thread_posix.c third_party/cJSON/cJSON.c
APP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRCS)) $(BUILD)/rules_builtin.o

CFLAGS   ?= -O2 -g
MBED_DEF := -DMBEDTLS_USER_CONFIG_FILE='"wiitube_mbedtls_user.h"'
INCS     := -Isrc/core -I$(MBED)/include -Ithird_party/cJSON
APP_FLAGS  := -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
              -Wno-unknown-warning-option -D_POSIX_C_SOURCE=200809L -DWIITUBE_HAVE_ZLIB
LIBS     := -lz -lpthread

ifeq ($(shell uname -s),Darwin)
APP_FLAGS += -D_DARWIN_C_SOURCE
endif

all: wiitube-cli

wiitube-cli: $(APP_OBJS) $(BUILD)/libmbedtls.a
	$(CC) -o $@ $(APP_OBJS) $(BUILD)/libmbedtls.a $(LIBS)

$(BUILD)/libmbedtls.a: $(MBED_OBJS)
	rm -f $@
	ar rcs $@ $^

$(BUILD)/mbedtls/%.o: $(MBED)/library/%.c src/core/wiitube_mbedtls_user.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c99 $(MBED_DEF) -Isrc/core -I$(MBED)/include -I$(MBED)/library -c $< -o $@

$(BUILD)/%.o: %.c $(wildcard src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_FLAGS) $(MBED_DEF) $(INCS) -c $< -o $@

# Embed rules/youtube.json as the fallback used when no rules file is found.
$(BUILD)/rules_builtin.c: rules/youtube.json
	@mkdir -p $(dir $@)
	{ echo 'const char yt_rules_builtin[] ='; \
	  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' $<; \
	  echo ';'; } > $@

$(BUILD)/rules_builtin.o: $(BUILD)/rules_builtin.c
	$(CC) $(CFLAGS) -c $< -o $@

# Player pipeline test against the system FFmpeg (needs libavformat-dev etc.).
# Run with: make test-media  (generates test clips with the ffmpeg CLI)
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil 2>/dev/null)
FFMPEG_LIBS   := $(shell pkg-config --libs libavformat libavcodec libavutil 2>/dev/null)
TEST_MEDIA_SRCS := tests/test_media.c src/core/media.c src/core/ring.c src/core/convert.c src/core/log.c \
                   src/core/vqueue.c \
                   src/platform/linux/thread_posix.c

$(BUILD)/test_media: $(TEST_MEDIA_SRCS) $(wildcard src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_FLAGS) $(FFMPEG_CFLAGS) -o $@ $(TEST_MEDIA_SRCS) $(FFMPEG_LIBS) -lpthread -lm

TEST_THUMBS_SRCS := tests/test_thumbs.c src/core/thumbs.c src/core/image.c src/core/convert.c src/core/http.c \
                    src/core/log.c src/platform/linux/net_linux.c src/platform/linux/thread_posix.c

$(BUILD)/test_thumbs: $(TEST_THUMBS_SRCS) $(BUILD)/libmbedtls.a $(wildcard src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_FLAGS) $(MBED_DEF) $(INCS) -Ithird_party/stb -o $@ $(TEST_THUMBS_SRCS) \
	    $(BUILD)/libmbedtls.a -lz -lpthread

test-thumbs: $(BUILD)/test_thumbs
	sh tests/run_thumbs_test.sh $(BUILD)

# Screenshots of the Wii UI rendered in software: make ui-preview (PNGs in build/linux/preview)
UI_PREVIEW_SRCS := tests/ui_preview.c src/ui/screens.c src/ui/ui_common.c src/ui/atlas_data.c \
                   src/platform/linux/soft_draw.c src/platform/wii/logo_data.c src/core/image.c src/core/convert.c

$(BUILD)/ui_preview: $(UI_PREVIEW_SRCS) $(wildcard src/ui/*.h src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_FLAGS) -Ithird_party/stb -o $@ $(UI_PREVIEW_SRCS)

ui-preview: $(BUILD)/ui_preview
	python3 -I tests/make_preview_thumbs.py $(BUILD)/preview-thumbs
	mkdir -p $(BUILD)/preview
	$(BUILD)/ui_preview $(BUILD)/preview-thumbs $(BUILD)/preview
	python3 -I tests/ppm2png.py $(BUILD)/preview

# Live preview with real search results and thumbnails (needs YouTube access; used in CI):
#   make build/linux/ui_live && build/linux/ui_live "wii sports" OUTDIR
UI_LIVE_SRCS := tests/ui_live.c src/ui/screens.c src/ui/ui_common.c src/ui/atlas_data.c \
                src/platform/linux/soft_draw.c src/platform/wii/logo_data.c src/core/image.c src/core/convert.c \
                src/core/thumbs.c src/core/http.c src/core/yt.c src/core/log.c src/platform/linux/net_linux.c \
                src/platform/linux/thread_posix.c third_party/cJSON/cJSON.c

$(BUILD)/ui_live: $(UI_LIVE_SRCS) $(BUILD)/libmbedtls.a $(BUILD)/rules_builtin.o $(wildcard src/ui/*.h src/core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_FLAGS) $(MBED_DEF) $(INCS) -Ithird_party/stb -o $@ $(UI_LIVE_SRCS) \
	    $(BUILD)/rules_builtin.o $(BUILD)/libmbedtls.a -lz -lpthread

test: test-media test-thumbs

test-media: $(BUILD)/test_media
	sh tests/make_clips.sh $(BUILD)/clips
	$(BUILD)/test_media $(BUILD)/clips/video.mp4 $(BUILD)/clips/audio.m4a $(BUILD)/clips/ref.rgb

clean:
	rm -rf build wiitube-cli

.PHONY: all clean test test-media test-thumbs ui-preview
