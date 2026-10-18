# Clove launcher
#   make host   -> build/clove-host  (desktop test build, writes screenshots)
#   make arm    -> build/clove       (static ARMv7 binary for the RG35XX)

ARM_TOOLCHAIN ?= /opt/arm-rpi-4.9.3-linux-gnueabihf   # any arm-linux-gnueabihf GCC (see BUILD.txt)
ARM_CC ?= $(ARM_TOOLCHAIN)/bin/arm-linux-gnueabihf-gcc
ARM_STRIP ?= $(ARM_TOOLCHAIN)/bin/arm-linux-gnueabihf-strip

COMMON = src/util.c src/plat_common.c src/gfx.c src/font.c src/config.c src/data.c src/import.c src/music.c src/musiclib.c src/playlists.c src/decoders.c src/video.c src/ui.c src/main.c third_party/cJSON.c
CFLAGS_COMMON = -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

.PHONY: all host arm web tablet clean

all: host arm

host: build/clove-host
arm: build/clove

build/clove-host: $(COMMON) src/plat_host.c src/*.h src/*.inc
	@mkdir -p build
	cc $(CFLAGS_COMMON) -O1 -g -fsanitize=address,undefined -o $@ $(COMMON) src/plat_host.c -lm -pthread

build/clove: $(COMMON) src/plat_dev.c src/*.h src/*.inc
	@mkdir -p build
	$(ARM_CC) $(CFLAGS_COMMON) -O2 -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math \
		-static -o $@ $(COMMON) src/plat_dev.c -lm -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
	$(ARM_STRIP) $@

# Browser build (WebAssembly) for trying Zappo X on a tablet or phone:
#   make web -> build/web/zappo.wasm (+ the page from web/, the kit's files)
WASI_SYSROOT ?= /home/claude/twin/wasi-sysroot
WEB_CC ?= clang --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT)
web: build/web/zappo.wasm

build/web/zappo.wasm: $(COMMON) src/plat_web.c src/music_web.inc src/web_compat.h src/*.h src/*.inc
	@mkdir -p build/web
	$(WEB_CC) $(CFLAGS_COMMON) -O2 -DZAPPO_NO_MAIN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS \
		-include src/web_compat.h -mexec-model=reactor -Wl,-z,stack-size=4194304 -Wl,--strip-debug \
		-o $@ $(COMMON) src/plat_web.c -lwasi-emulated-signal -lwasi-emulated-process-clocks -lm

# Full speed on a tablet/phone (Termux) or any Linux PC, screen in the browser:
#   make tablet -> build/zappo-tablet (+ build/web-native, the page)
CC_TABLET ?= cc
tablet: build/zappo-tablet

build/zappo-tablet: $(COMMON) src/plat_tablet.c src/*.h src/*.inc
	@mkdir -p build
	$(CC_TABLET) $(CFLAGS_COMMON) -O2 -DZAPPO_NO_MAIN -DZAPPO_TABLET -o $@ $(COMMON) src/plat_tablet.c -lm -pthread

clean:
	rm -rf build

# ARM build of the test harness: runs the shared code under qemu-arm
build/clove-armtest: $(COMMON) src/plat_host.c src/*.h src/*.inc
	@mkdir -p build
	$(ARM_CC) $(CFLAGS_COMMON) -O2 -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math \
		-static -o $@ $(COMMON) src/plat_host.c -lm -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -pthread
