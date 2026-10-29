#!/usr/bin/env bash
# Records the installer demo video (what players see on Windows) without touching any game files:
# DSCoopSetup.exe /preview renders a scripted session at exactly 30 fps under Wine + Xvfb, then the
# frames go on a desktop-like background and ffmpeg makes MP4 + GIF.
# Needs: wine64 xvfb imagemagick ffmpeg python3-numpy python3-pil
# Usage: scripts/record-installer-demo.sh [DSCoopSetup.exe] [out_dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SETUP="${1:-$ROOT/build/mingw-cross/bin/DSCoopSetup.exe}"
OUT="${2:-$ROOT/build/demo}"
export PATH="/usr/lib/wine:$PATH" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
export WINEPREFIX="${WINEPREFIX:-$ROOT/build/wineprefix-demo}"
[ -d "$WINEPREFIX/drive_c" ] || wine64 wineboot -i >/dev/null 2>&1 || true
C="$WINEPREFIX/drive_c"
# Fake Steam: the game in a second library on D:, Steam name with Cyrillic.
mkdir -p "$ROOT/build/demo-ddrive" && ln -sfn "$ROOT/build/demo-ddrive" "$WINEPREFIX/dosdevices/d:"
STEAM="$C/Program Files (x86)/Steam"; LIB="$ROOT/build/demo-ddrive/SteamLibrary"; GAME="$LIB/steamapps/common/DEATH STRANDING DIRECTORS CUT"
mkdir -p "$STEAM/steamapps" "$STEAM/config" "$GAME" "$LIB/steamapps"
printf '"libraryfolders"\n{\n "0" { "path" "C:\\\\Program Files (x86)\\\\Steam" }\n "1" { "path" "D:\\\\SteamLibrary" "apps" { "1850570" "1" } }\n}\n' > "$STEAM/steamapps/libraryfolders.vdf"
printf '"AppState"\n{\n "appid" "1850570"\n "installdir" "DEATH STRANDING DIRECTORS CUT"\n}\n' > "$LIB/steamapps/appmanifest_1850570.acf"
printf '"users" { "1" { "PersonaName" "Сэм Бриджес" "MostRecent" "1" } }\n' > "$STEAM/config/loginusers.vdf"
[ -f "$GAME/ds.exe" ] || head -c 5000 /dev/urandom > "$GAME/ds.exe"
wine64 reg add 'HKCU\Software\Valve\Steam' /v SteamPath /t REG_SZ /d 'c:/program files (x86)/steam' /f >/dev/null 2>&1
wineserver -k 2>/dev/null || true   # a wineserver started without DISPLAY would make the GUI fail
cp "$SETUP" "$C/DSCoopSetup.exe"
rm -rf "$C/frames"
# Wine/Xvfb teardown can return non-zero ("X connection broken") even when every frame was written,
# so success is judged by the frames, not by the exit code.
LANG=${DEMO_LANG:-ru_RU.UTF-8} LC_ALL=${DEMO_LANG:-ru_RU.UTF-8} xvfb-run -a -s "-screen 0 1280x800x24" \
  wine64 'C:\DSCoopSetup.exe' '/preview=C:\frames' || true
N=$(ls "$C/frames" 2>/dev/null | wc -l)
[ "$N" -ge 400 ] || { echo "preview produced only $N frames"; exit 1; }
mkdir -p "$OUT"
rm -rf "$OUT/composed" && python3 "$ROOT/tools/art/compose_demo.py" "$C/frames" "$OUT/composed"
ffmpeg -loglevel error -y -framerate 30 -i "$OUT/composed/f_%04d.png" -c:v libx264 -pix_fmt yuv420p -crf 18 -preset slow -movflags +faststart "$OUT/DSCoopSetup-demo.mp4"
ffmpeg -loglevel error -y -framerate 30 -i "$OUT/composed/f_%04d.png" -vf "fps=15,scale=800:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=192[p];[b][p]paletteuse=dither=sierra2_4a" "$OUT/DSCoopSetup-demo.gif"
cp "$C/frames/frame_0060.png" "$OUT/welcome.png"; cp "$C/frames/frame_0255.png" "$OUT/progress.png"; cp "$C/frames/frame_0420.png" "$OUT/done.png"
ls -la "$OUT"
