#!/bin/bash
# Downloads what wired Android mirroring needs into deps/android (checksums are verified):
#   - scrcpy-server (runs on the phone, Apache 2.0)
#   - adb.exe, AdbWinApi.dll, AdbWinUsbApi.dll (Android platform tools, taken from the scrcpy Windows release)
# They are embedded into Mirrorly.exe by build_windows.sh.
set -e
cd "$(dirname "$0")"
V=5.0.1
SERVER_SHA=764eb6f79811d5211fe9df341120882ba9994c7a61b897d7bf3fb662e53bc536
WIN_SHA=b12a2c4ee8be317422451fc7dcf8ee20a71b5ea7ef9ad73ddd825a25316227a5
URL=https://github.com/Genymobile/scrcpy/releases/download/v$V
OUT=deps/android
mkdir -p $OUT/tmp
if [ ! -f $OUT/scrcpy-server ] || ! echo "$SERVER_SHA  $OUT/scrcpy-server" | sha256sum -c --quiet; then
  curl -fsSL -o $OUT/scrcpy-server "$URL/scrcpy-server-v$V"
  echo "$SERVER_SHA  $OUT/scrcpy-server" | sha256sum -c --quiet
fi
if [ ! -f $OUT/adb.exe ]; then
  curl -fsSL -o $OUT/tmp/scrcpy-win64.zip "$URL/scrcpy-win64-v$V.zip"
  echo "$WIN_SHA  $OUT/tmp/scrcpy-win64.zip" | sha256sum -c --quiet
  unzip -o -j -q $OUT/tmp/scrcpy-win64.zip 'scrcpy-win64-*/adb.exe' 'scrcpy-win64-*/AdbWinApi.dll' 'scrcpy-win64-*/AdbWinUsbApi.dll' -d $OUT
fi
rm -rf $OUT/tmp
ls -la $OUT
