#!/usr/bin/env bash
# Runs DSCoopSetup.exe under Wine against a fake Steam install and checks every scenario players hit:
# auto-detection in a second Steam library, Steam nick, save backup, Apps & features entry,
# another mod's version.dll, reinstall, uninstall from the kept copy, "game is running" guard.
# Needs: wine64 (apt install wine64). Usage: scripts/test-installer-wine.sh [path/to/DSCoopSetup.exe]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SETUP="${1:-$ROOT/build/mingw-cross/bin/DSCoopSetup.exe}"
CLIENT="${CLIENT:-$(dirname "$SETUP")/dscoop_testclient.exe}"
export PATH="/usr/lib/wine:$PATH"
export WINEPREFIX="${WINEPREFIX:-$ROOT/build/wineprefix}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
rm -rf "$WINEPREFIX"
wine64 wineboot -i >/dev/null 2>&1 || true
C="$WINEPREFIX/drive_c"
STEAM="$C/Program Files (x86)/Steam"; LIB="$C/SteamLib2"; GAME="$LIB/steamapps/common/DEATH STRANDING DIRECTORS CUT"
WGAME='C:\SteamLib2\steamapps\common\DEATH STRANDING DIRECTORS CUT'
mkdir -p "$STEAM/steamapps" "$STEAM/config" "$GAME" "$LIB/steamapps"
printf '"libraryfolders"\n{\n "0" { "path" "C:\\\\Program Files (x86)\\\\Steam" }\n "1" { "path" "C:\\\\SteamLib2" "apps" { "1850570" "1" } }\n}\n' > "$STEAM/steamapps/libraryfolders.vdf"
printf '"AppState"\n{\n "appid" "1850570"\n "name" "DEATH STRANDING DIRECTOR%sS CUT"\n "installdir" "DEATH STRANDING DIRECTORS CUT"\n}\n' "'" > "$LIB/steamapps/appmanifest_1850570.acf"
printf '"users" { "76561198000000001" { "AccountName" "sam" "PersonaName" "SteamSam" "MostRecent" "1" } }\n' > "$STEAM/config/loginusers.vdf"
head -c 5000 /dev/urandom > "$GAME/ds.exe"
LAD=$(ls -d "$C"/users/*/AppData/Local | head -1)
mkdir -p "$LAD/KojimaProductions/DeathStrandingDC/1"; echo SAVE > "$LAD/KojimaProductions/DeathStrandingDC/1/checkpoint.dat"
wine64 reg add 'HKCU\Software\Valve\Steam' /v SteamPath /t REG_SZ /d 'c:/program files (x86)/steam' /f >/dev/null 2>&1
cp "$SETUP" "$C/DSCoopSetup.exe"

fail() { echo "FAIL: $*"; for f in "$C"/*.log; do echo "== $f"; cat "$f"; done; exit 1; }
run() { local code=0; timeout 120 wine64 'C:\DSCoopSetup.exe' "$@" || code=$?; echo "$code"; }

echo "1. fresh install (auto-detect)"
[ "$(run /S '/log=C:\1.log')" = 0 ] || fail "install"
for f in version.dll dscoop/plugins/DSCoop.dll dscoop/data/offsets.json dscoop/dscoop.json dscoop/install.json dscoop/DSCoopSetup.exe; do
  [ -f "$GAME/$f" ] || fail "missing $f"
done
grep -q SteamSam "$GAME/dscoop/dscoop.json" || fail "Steam nick not used"
find "$GAME/dscoop/backups" -name checkpoint.dat | grep -q . || fail "saves not backed up"
wine64 reg query 'HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\DSCoop' >/dev/null 2>&1 || fail "no Apps & features entry"

echo "2. uninstall from the kept copy (what Apps & features runs)"
code=0; timeout 60 wine64 "$WGAME\\dscoop\\DSCoopSetup.exe" /uninstall /S '/log=C:\2.log' || code=$?
[ "$code" = 0 ] || fail "uninstall"
[ ! -f "$GAME/version.dll" ] || fail "version.dll left behind"
grep -q SteamSam "$GAME/dscoop/dscoop.json" || fail "settings were not kept"
! wine64 reg query 'HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\DSCoop' >/dev/null 2>&1 || fail "registry entry left behind"

echo "3. another mod's version.dll + chosen nick + relay"
cp "$C/windows/system32/version.dll" "$GAME/version.dll"; FOREIGN=$(md5sum < "$GAME/version.dll")
[ "$(run /S /nobackup '/nick=Fragile' '/relay=relay.example.com:27005' '/log=C:\3.log')" = 0 ] || fail "install over foreign mod"
[ "$(md5sum < "$GAME/dscoop/chain/version_original.dll")" = "$FOREIGN" ] || fail "foreign dll not chained"
grep -q Fragile "$GAME/dscoop/dscoop.json" && grep -q 27005 "$GAME/dscoop/dscoop.json" || fail "nick/relay not written"

echo "4. reinstall keeps the chosen nick and does not chain our own dll"
[ "$(run /S /nobackup '/log=C:\4.log')" = 0 ] || fail "reinstall"
grep -q Fragile "$GAME/dscoop/dscoop.json" || fail "reinstall replaced the nick"
! grep -qi warning "$C/4.log" || fail "warnings on reinstall"
[ "$(md5sum < "$GAME/dscoop/chain/version_original.dll")" = "$FOREIGN" ] || fail "chain overwritten"

echo "5. refuses while the game is running"
if [ -f "$CLIENT" ]; then
  cp "$CLIENT" "$GAME/ds.exe"
  (timeout 40 wine64 "$WGAME\\ds.exe" host --port 27999 --duration 30 --quiet >/dev/null 2>&1 &)
  sleep 6
  [ "$(run /S /nobackup '/log=C:\5.log')" = 3 ] || fail "installed while the game was running"
  wineserver -k 2>/dev/null || true; sleep 1
else
  echo "   (skipped: no dscoop_testclient.exe to impersonate the game)"
fi

echo "6. uninstall restores the other mod"
[ "$(run /S /uninstall '/log=C:\6.log')" = 0 ] || fail "uninstall"
[ "$(md5sum < "$GAME/version.dll")" = "$FOREIGN" ] || fail "foreign version.dll not restored"

wineserver -k 2>/dev/null || true
echo "OK — installer passed all Wine scenarios"
