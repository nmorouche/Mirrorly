#!/bin/bash
# Builds the minimal static FFmpeg used by Mirrorly (h264/aac/alac decoders + swscale).
#   ./build_ffmpeg.sh win    -> ffmpeg-win/install   (MinGW-w64 cross build, for Mirrorly.exe)
#   ./build_ffmpeg.sh linux  -> ffmpeg-linux/install (native, for the tests)
# Needs: git, nasm, make, and x86_64-w64-mingw32-gcc for the Windows build.
set -e
T=${1:-win}
ROOT=$(cd "$(dirname "$0")" && pwd)/deps
mkdir -p "$ROOT" && cd "$ROOT"
[ -d ffmpeg-src ] || git clone --depth 1 --branch n7.1 https://github.com/FFmpeg/FFmpeg.git ffmpeg-src
OUT=$ROOT/ffmpeg-$T
mkdir -p "$OUT/obj" && cd "$OUT/obj"
COMMON="--disable-everything --disable-programs --disable-doc --disable-network --disable-autodetect
 --disable-avdevice --disable-avformat --disable-avfilter --disable-swresample
 --enable-avcodec --enable-avutil --enable-swscale
 --enable-decoder=h264,aac,alac --enable-parser=h264,aac
 --enable-static --disable-shared --enable-pic --disable-debug --enable-asm --enable-x86asm
 --prefix=$OUT/install"
if [ "$T" = win ]; then
  ../../ffmpeg-src/configure $COMMON --arch=x86_64 --target-os=mingw32 --cross-prefix=x86_64-w64-mingw32- --enable-w32threads
else
  ../../ffmpeg-src/configure $COMMON --enable-pthreads
fi
make -j"$(nproc)"
make install
echo "FFmpeg installed to $OUT/install"
