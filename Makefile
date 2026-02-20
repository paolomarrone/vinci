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

.PHONY: all clean
