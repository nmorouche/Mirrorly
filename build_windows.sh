#!/bin/bash
# Cross-compiles Mirrorly.exe (Windows x64) with MinGW-w64. FFmpeg is linked statically.
# Optional: VERSION=1.2.3 ./build_windows.sh  (defaults to 1.0.0)
# Wired Android mirroring needs deps/android (run ./fetch_android_deps.sh once).
set -e
cd "$(dirname "$0")"
FF=${FF_WIN:-$(pwd)/deps/ffmpeg-win/install}
CC=x86_64-w64-mingw32-gcc
VERSION=${VERSION:-1.0.0}
VERSION=${VERSION#v}
IFS=. read -r V1 V2 V3 _ <<< "$VERSION"
VER_NUM="${V1:-0},${V2:-0},${V3:-0},0"
CXX=x86_64-w64-mingw32-g++-posix
OUT=out/win; mkdir -p $OUT/obj
INC="-I src/core -I src/app -I third_party/qrcodegen -I third_party/playfair -I third_party/monocypher -I third_party/tinyaes -I $FF/include"
BUILD=${BUILD:-$(git rev-parse --short HEAD 2>/dev/null || echo dev)}
CFLAGS="-O2 -DNDEBUG -DMIRRORLY_BUILD=\"$BUILD\" -DMIRRORLY_VERSION=\"$VERSION\" -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 $INC"
for f in third_party/playfair/*.c third_party/monocypher/*.c third_party/tinyaes/aes.c third_party/qrcodegen/qrcodegen.c src/core/native_media.c; do
  o=$OUT/obj/$(basename $f .c).o; [ $o -nt $f ] || $CC $CFLAGS -c $f -o $o
done
for f in src/core/*.cpp src/app/*.cpp; do
  o=$OUT/obj/$(basename $f .cpp).o
  $CXX -std=c++17 -Wall -Wno-unused-private-field -Wno-missing-field-initializers -municode $CFLAGS -c $f -o $o
done
ANDROID=${ANDROID_DEPS:-$(pwd)/deps/android}
for f in adb.exe AdbWinApi.dll AdbWinUsbApi.dll scrcpy-server; do
  [ -f "$ANDROID/$f" ] || { echo "missing $ANDROID/$f: run ./fetch_android_deps.sh"; exit 1; }
done
cat > $OUT/obj/android.rc <<RC
200 RCDATA "$ANDROID/adb.exe"
201 RCDATA "$ANDROID/AdbWinApi.dll"
202 RCDATA "$ANDROID/AdbWinUsbApi.dll"
210 RCDATA "$ANDROID/scrcpy-server"
RC
x86_64-w64-mingw32-windres $OUT/obj/android.rc -O coff -o $OUT/obj/android_res.o
x86_64-w64-mingw32-windres "-DVER_STR=\\\"$VERSION\\\"" "-DVER_NUM=$VER_NUM" res/app.rc -O coff -o $OUT/obj/res.o
$CXX -municode -mwindows -static -s -o $OUT/Mirrorly.exe $OUT/obj/*.o \
  -L$FF/lib -lswscale -lavcodec -lavutil \
  -ld2d1 -ldwrite -lwindowscodecs -ldwmapi -lwinmm -lole32 -luuid -lshell32 -lshlwapi \
  -lws2_32 -liphlpapi -lbcrypt -lgdi32 -luser32
ls -la $OUT/Mirrorly.exe
