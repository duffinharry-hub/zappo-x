#!/bin/sh
# Builds the twin's graphics pack: what stands in for the RG40XX H's Mali GPU
# on the emulated machine.  Knulli's programs (EmulationStation, RetroArch,
# Zappo X) draw through SDL2 and OpenGL ES; on the handheld that is the Mali
# blob on its framebuffer, on the twin it is:
#   libdrm 2.4.124, Mesa 25.3.6 (softpipe: OpenGL ES in software, GBM + EGL on
#   the twin's KMS screen), SDL 2.30.12 (Knulli's version) with KMS/DRM video,
#   and libmali.so.0, a stand-in that pulls in Mesa's EGL/GLES.
# Output: $OUT (laid over Knulli at start-up by boot/mkinitrd.py).
#
# Needs: the sysroot (mksysroot.py), sources in $SRC (libdrm, mesa, sdl2, mako, alsa-lib 1.2.13),
# meson (QEMU's build venv has one), cmake, ninja, clang/lld.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TWIN=${TWIN:-/home/claude/twin}
SRC=${SRC:-$TWIN/src}
B=${B:-$TWIN/build}
S=${SYSROOT:-$TWIN/sysroot}
OUT=${OUT:-$TWIN/gpu-stage}
MESON=${MESON:-$TWIN/src/qemu/build/pyvenv/bin/meson}
export PATH=$TWIN/tools/bin:$PATH PYTHONPATH=$SRC/mako PKG_CONFIG_SYSROOT_DIR=$S PKG_CONFIG_LIBDIR=$S/usr/lib/pkgconfig
CROSS=$HERE/aarch64-knulli.ini

# libdrm (Knulli has none: the Mali blob doesn't use it)
$MESON setup $B/libdrm $SRC/libdrm --cross-file $CROSS --prefix=/usr --libdir=lib --buildtype=release \
  -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
  -Dfreedreno=disabled -Dvc4=disabled -Detnaviv=disabled -Dexynos=disabled -Domap=disabled -Dtegra=disabled \
  -Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled -Dtests=false -Dudev=false --reconfigure 2>/dev/null \
  || $MESON setup $B/libdrm $SRC/libdrm --cross-file $CROSS --prefix=/usr --libdir=lib --buildtype=release \
  -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled -Dvmwgfx=disabled \
  -Dfreedreno=disabled -Dvc4=disabled -Detnaviv=disabled -Dexynos=disabled -Domap=disabled -Dtegra=disabled \
  -Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled -Dtests=false -Dudev=false
ninja -C $B/libdrm && DESTDIR=$S ninja -C $B/libdrm install

# Mesa: softpipe only, no LLVM, GBM + EGL + GLES 1/2/3
[ -d $B/mesa ] || $MESON setup $B/mesa $SRC/mesa --cross-file $CROSS --prefix=/usr --libdir=lib --buildtype=release -Db_ndebug=true \
  -Dplatforms= -Dgallium-drivers=softpipe -Dvulkan-drivers= -Dgbm=enabled -Degl=enabled -Dgles1=enabled -Dgles2=enabled \
  -Dopengl=true -Dglx=disabled -Dglvnd=disabled -Dllvm=disabled -Ddraw-use-llvm=false -Dvalgrind=disabled \
  -Dlibunwind=disabled -Dlmsensors=disabled -Dzstd=disabled -Dexpat=enabled -Dxmlconfig=disabled -Dshader-cache=disabled \
  -Dgallium-va=disabled -Dgallium-rusticl=false -Dvideo-codecs= -Dtools= -Dbuild-tests=false -Dmicrosoft-clc=disabled \
  -Dspirv-tools=disabled -Dmesa-clc=auto -Dteflon=false -Dhtml-docs=disabled -Dselinux=false -Ddisplay-info=disabled \
  -Dperfetto=false -Dandroid-libbacktrace=disabled
ninja -C $B/mesa && DESTDIR=$S ninja -C $B/mesa install

# ALSA 1.2.13 headers (Knulli has the library, not the headers), for SDL's sound
A=$S/usr/include/alsa
if [ ! -f $A/asoundlib.h ]; then
  mkdir -p $A && cp $SRC/alsa-lib/include/*.h $A/ && cp -r $SRC/alsa-lib/include/sound $A/
  { cat $SRC/alsa-lib/include/asoundlib-head.h; echo '#include <endian.h>'
    for h in asoundef version global input output error conf pcm rawmidi ump timer hwdep control mixer seq_event seq seqmid seq_midi_event; do
      echo "#include <alsa/$h.h>"; done
    cat $SRC/alsa-lib/include/asoundlib-tail.h; } > $A/asoundlib.h
  printf '#define SND_LIB_MAJOR 1\n#define SND_LIB_MINOR 2\n#define SND_LIB_SUBMINOR 13\n#define SND_LIB_EXTRAVER 1000000\n#define SND_LIB_VER(maj, min, sub) (((maj)<<16)|((min)<<8)|(sub))\n#define SND_LIB_VERSION SND_LIB_VER(1, 2, 13)\n#define SND_LIB_VERSION_STR "1.2.13"\n' > $A/version.h
  printf 'prefix=/usr\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: alsa\nDescription: ALSA\nVersion: 1.2.13\nLibs: -L${libdir} -lasound\nCflags: -I${includedir}\n' > $S/usr/lib/pkgconfig/alsa.pc
fi

# SDL 2.30.12, the version Knulli ships, with KMS/DRM instead of Mali
[ -d $B/sdl2 ] || cmake -S $SRC/sdl2 -B $B/sdl2 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$HERE/aarch64-knulli.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
  -DSDL_KMSDRM=ON -DSDL_KMSDRM_SHARED=ON -DSDL_OPENGLES=ON -DSDL_OPENGL=OFF -DSDL_VULKAN=OFF -DSDL_X11=OFF \
  -DSDL_WAYLAND=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_DIRECTFB=OFF -DSDL_OFFSCREEN=ON -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON \
  -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_OSS=OFF -DSDL_DISKAUDIO=ON \
  -DSDL_DUMMYAUDIO=ON -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_HIDAPI=ON -DSDL_LIBSAMPLERATE=OFF
ninja -C $B/sdl2

# the pack
rm -rf $OUT && mkdir -p $OUT/usr/lib/gbm
DESTDIR=$OUT ninja -C $B/mesa install >/dev/null
DESTDIR=$OUT ninja -C $B/sdl2 install >/dev/null
cp -a $S/usr/lib/libdrm.so.2* $OUT/usr/lib/
clang --target=aarch64-linux-gnu --sysroot=$S -fuse-ld=lld -shared -fPIC -O2 -o $OUT/usr/lib/libmali.so.0 \
  $HERE/mali_stub.c -Wl,-soname,libmali.so.0 -Wl,--no-as-needed -L$OUT/usr/lib -lEGL -lGLESv2 -lGLESv1_CM
rm -rf $OUT/usr/include $OUT/usr/lib/pkgconfig $OUT/usr/lib/cmake $OUT/usr/share $OUT/usr/bin $OUT/usr/lib/*.a
for f in $OUT/usr/lib/*.so* $OUT/usr/lib/gbm/*.so; do [ -L "$f" ] || llvm-strip --strip-unneeded "$f"; done
du -sh $OUT
