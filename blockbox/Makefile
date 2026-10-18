# Blockbox libretro core
ARM_TOOLCHAIN ?= /opt/arm-rpi-4.9.3-linux-gnueabihf   # any arm-linux-gnueabihf GCC (see BUILD.txt)
ARM_CC ?= $(ARM_TOOLCHAIN)/bin/arm-linux-gnueabihf-gcc
ARM_STRIP ?= $(ARM_TOOLCHAIN)/bin/arm-linux-gnueabihf-strip

# No C library: the core must load in GarlicOS's uClibc RetroArch as well as glibc ones.
CORE_FLAGS = -std=gnu99 -Wall -Wextra -ffreestanding -fno-stack-protector -fno-tree-loop-distribute-patterns \
	-fvisibility=hidden -fno-math-errno -fPIC -fno-builtin-memset -fno-builtin-memcpy
# GarlicOS's RetroArch and cores use the soft-float ABI (VFP/NEON instructions, integer calling convention).
ARM_FLAGS = -O2 -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=softfp -ffast-math -fno-unsafe-math-optimizations -DBLOCKBOX_NO_LIBGCC

.PHONY: all host arm test clean
all: host arm

host: build/blockbox_libretro_host.so build/frontend-host
arm: build/blockbox_libretro.so build/frontend-arm

build/blockbox_libretro_host.so: src/blockbox.c src/font6x10.h
	@mkdir -p build
	cc $(CORE_FLAGS) -O2 -shared -nostdlib -o $@ $< -lgcc -Wl,--exclude-libs,ALL -Wl,-z,defs

build/frontend-host: tests/frontend.c src/blockbox.c
	@mkdir -p build
	cc -O2 -g -o $@ tests/frontend.c src/blockbox.c -fno-tree-loop-distribute-patterns -fno-math-errno -lm

build/blockbox_libretro.so: src/blockbox.c src/font6x10.h
	@mkdir -p build
	$(ARM_CC) $(CORE_FLAGS) $(ARM_FLAGS) -nostdinc -isystem $$($(ARM_CC) -print-file-name=include) -Isrc/freestanding \
		-shared -nostdlib -o $@ $< -Wl,-z,defs -Wl,--hash-style=both -Wl,-soname,blockbox_libretro.so
	$(ARM_STRIP) --strip-unneeded $@

build/frontend-arm: tests/frontend.c src/blockbox.c
	@mkdir -p build
	$(ARM_CC) -O2 -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -static -std=gnu99 -o $@ tests/frontend.c src/blockbox.c -fno-tree-loop-distribute-patterns -fno-math-errno -lm

clean:
	rm -rf build
