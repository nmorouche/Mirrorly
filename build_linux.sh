#!/bin/bash
# Linux build of the engine + test tools (used for automated testing).
set -e
cd "$(dirname "$0")"
FF=${FF_LINUX:-$(pwd)/deps/ffmpeg-linux/install}
OUT=out/linux; mkdir -p $OUT/obj
CFLAGS="-O2 -g -I src/core -I third_party/playfair -I third_party/monocypher -I third_party/tinyaes -I $FF/include"
for f in third_party/playfair/*.c third_party/monocypher/*.c third_party/tinyaes/aes.c src/core/native_media.c; do
  o=$OUT/obj/$(basename $f .c).o; [ $o -nt $f ] || gcc $CFLAGS -c $f -o $o
done
NEWEST_H=$(ls -t src/core/*.h | head -1)
for f in src/core/*.cpp; do
  o=$OUT/obj/$(basename $f .cpp).o; [ $o -nt $f ] && [ $o -nt $NEWEST_H ] || g++ -std=c++17 -Wall -Wno-unused-private-field $CFLAGS -c $f -o $o
done
LIBS="-L$FF/lib -lswscale -lavcodec -lavutil -lpthread -lm"
for t in tests/*.cpp; do
  n=$(basename $t .cpp)
  g++ -std=c++17 $CFLAGS $t $OUT/obj/*.o -o $OUT/$n $LIBS
done
echo build ok
