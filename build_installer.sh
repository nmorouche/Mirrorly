#!/bin/bash
# Builds out/win/Mirrorly-Setup.exe from out/win/Mirrorly.exe with NSIS (sudo apt install nsis).
# Run ./build_windows.sh first, with the same VERSION.
#   VERSION=1.2.3 ./build_installer.sh
# Optional: SIGN='<command> "%1"' signs the uninstaller and the setup as they are built
# (sign Mirrorly.exe itself before running this, so the installed copy is signed too).
set -e
cd "$(dirname "$0")"
VERSION=${VERSION:-1.0.0}
VERSION=${VERSION#v}
IFS=. read -r V1 V2 V3 _ <<< "$VERSION"
REPO=${REPO:-nmorouche/Mirrorly}
EXE=$(realpath "${EXE:-out/win/Mirrorly.exe}")
OUT=$(pwd)/out/win/Mirrorly-Setup.exe
[ -f "$EXE" ] || { echo "missing $EXE: run ./build_windows.sh first"; exit 1; }
args=(-V2 "-DVERSION=$VERSION" "-DVERNUM=${V1:-0}.${V2:-0}.${V3:-0}.0" "-DREPO=$REPO" "-DEXE=$EXE" "-DOUTFILE=$OUT")
[ -n "$SIGN" ] && args+=("-DSIGN=$SIGN")
makensis "${args[@]}" res/installer/mirrorly.nsi
ls -la "$OUT"
