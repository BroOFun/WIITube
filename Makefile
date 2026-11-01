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
            third_party/cJSON/cJSON.c
APP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRCS)) $(BUILD)/rules_builtin.o

CFLAGS   ?= -O2 -g
MBED_DEF := -DMBEDTLS_USER_CONFIG_FILE='"wiitube_mbedtls_user.h"'
INCS     := -Isrc/core -I$(MBED)/include -Ithird_party/cJSON
APP_FLAGS  := -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
              -Wno-unknown-warning-option -D_POSIX_C_SOURCE=200809L -DWIITUBE_HAVE_ZLIB
LIBS     := -lz

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

clean:
	rm -rf build wiitube-cli

.PHONY: all clean
