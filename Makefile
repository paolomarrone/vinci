#
# Vinci
#
# Copyright (C) 2025 Orastron Srl unipersonale
#
# Vinci is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3 of the License.
#
# Vinci is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Vinci.  If not, see <http://www.gnu.org/licenses/>.
#
# File author: Paolo Marrone
#

UNAME := $(shell uname -s)

ifeq ($(UNAME),Linux)
	VINCI_SRC    = vinci-xcb.c
	EXTRAOPTIONS = -lxcb
else ifeq ($(UNAME),Darwin)
	VINCI_SRC    = vinci-cocoa.m
	EXTRAOPTIONS = -framework Cocoa -lobjc
else ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME)))
	VINCI_SRC    = vinci-win32.c
	EXTRAOPTIONS = -mwindows
else
$(error Unsupported platform '$(UNAME)')
endif

CC = gcc
CFLAGS = -std=c99 -O3
STRICT = -Wall -Wextra -pedantic -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wuninitialized -Wmissing-declarations

BUILD_DIR = build

all: $(BUILD_DIR)/test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test: test.c $(VINCI_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(STRICT) $(VINCI_SRC) test.c -o $@ $(EXTRAOPTIONS)

clean:
	rm -rf $(BUILD_DIR)

WEB_CC ?= clang
WEB_CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
WEB_FLAGS = --target=wasm32-unknown-unknown -ffreestanding -fno-builtin -I. -Iweb
WEB_API = vinci_new vinci_destroy vinci_idle window_new window_free window_draw \
	window_get_handle window_get_width window_get_height window_resize window_move \
	window_show window_hide window_set_data window_get_data
WEB_LDFLAGS = -nostdlib -Wl,--no-entry,--export-memory,--fatal-warnings \
	-Wl,-z,stack-size=65536,--initial-memory=131072,--max-memory=67108864 \
	$(foreach name,$(WEB_API),-Wl,--export=$(name))

web: $(BUILD_DIR)/web/demo.wasm $(BUILD_DIR)/web/index.html $(BUILD_DIR)/web/vinci-web.js

$(BUILD_DIR)/web/demo.wasm: vinci-web.c vinci.h web/demo.c web/memory.c web/stdlib.h
	mkdir -p $(dir $@)
	$(WEB_CC) $(WEB_FLAGS) $(WEB_CFLAGS) vinci-web.c web/demo.c web/memory.c $(WEB_LDFLAGS) -o $@

$(BUILD_DIR)/web/index.html: web/index.html
	mkdir -p $(dir $@)
	cp $< $@

$(BUILD_DIR)/web/vinci-web.js: vinci-web.js
	mkdir -p $(dir $@)
	cp $< $@

$(BUILD_DIR)/web/test.wasm: vinci-web.c vinci.h tests/web.c web/memory.c web/stdlib.h
	mkdir -p $(dir $@)
	$(WEB_CC) $(WEB_FLAGS) $(WEB_CFLAGS) vinci-web.c tests/web.c web/memory.c $(WEB_LDFLAGS) -o $@

test-web: web $(BUILD_DIR)/web/test.wasm
	node tests/web.mjs $(BUILD_DIR)/web

.PHONY: all clean web test-web
