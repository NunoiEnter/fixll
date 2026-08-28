#!/usr/bin/env bash
#
# fixll — install the LimeLight Lemonade Jam bootStrap shim (+ optional English patch)
#
# Based on / credits: sst311212/FuckBootStrap  (https://github.com/sst311212/FuckBootStrap)
# This script wraps the prebuilt 32-bit VERSION.dll (a Windows PE binary, so it is
# distro-independent) and optionally the English MTL patch (VNDB r142645).
#
# Usage:
#   ./install.sh /path/to/limelight_lj
#   ./install.sh /path/to/limelight_lj --english
#   ./install.sh /path/to/limelight_lj --english --heroic
#
set -euo pipefail

ENGLISH=0
HEROIC=0
GAMEDIR=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<EOF
fixll installer

Usage:
  $0 <game_dir> [--english] [--heroic]

  <game_dir>   path to the game folder containing limelight_lj.exe
  --english    also download + install the English MTL patch (VNDB r142645, V1.20)
  --heroic     auto-set WINEDLLOVERRIDES=version=n in the game's Heroic config
               (without this flag, the setting is only printed)

The VERSION.dll shipped here is a 32-bit shim that works on EVERY Linux distro
(NixOS, Debian/Ubuntu, Arch, Fedora, openSUSE, Alpine, ...). No toolchain needed.
EOF
}

for a in "$@"; do
  case "$a" in
    --english) ENGLISH=1 ;;
    --heroic)  HEROIC=1 ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "unknown flag: $a"; usage; exit 1 ;;
    *) GAMEDIR="$a" ;;
  esac
done

if [ -z "$GAMEDIR" ]; then echo "ERROR: game_dir required"; usage; exit 1; fi
if [ ! -d "$GAMEDIR" ]; then echo "ERROR: not a directory: $GAMEDIR"; exit 1; fi

DLL="$SCRIPT_DIR/VERSION.dll"
if [ ! -f "$DLL" ]; then echo "ERROR: VERSION.dll not found next to this script"; exit 1; fi

# --- tool helpers -----------------------------------------------------------
need() { command -v "$1" >/dev/null 2>&1; }
pm_install() {
  local pkg="$1"
  if need apt-get;      then sudo apt-get update -y && sudo apt-get install -y "$pkg";
  elif need dnf;       then sudo dnf install -y "$pkg";
  elif need pacman;    then sudo pacman -S --needed --noconfirm "$pkg";
  elif need zypper;    then sudo zypper install -y "$pkg";
  elif need apk;       then sudo apk add "$pkg";
  else echo "  (no supported package manager — install '$pkg' manually)"; return 1; fi
}

# --- 1) install the shim ----------------------------------------------------
echo "==> Installing shim (VERSION.dll) into: $GAMEDIR"
if [ -f "$GAMEDIR/version.dll" ] || [ -f "$GAMEDIR/Version.dll" ]; then
  BAK="$GAMEDIR/version.dll.bak"
  [ -f "$BAK" ] && BAK="$GAMEDIR/version.dll.bak.$(date +%s)"
  cp -f "$GAMEDIR/version.dll" "$BAK" 2>/dev/null || cp -f "$GAMEDIR/Version.dll" "$BAK" 2>/dev/null || true
  echo "    backed up existing version.dll -> $BAK"
fi
cp -f "$DLL" "$GAMEDIR/version.dll"
echo "    done."

# --- 2) optional English patch ----------------------------------------------
if [ "$ENGLISH" -eq 1 ]; then
  echo "==> Installing English MTL patch (r142645, V1.20)"
  need curl || pm_install curl || { echo "ERROR: curl required for --english"; exit 1; }
  need 7z   || need 7za || pm_install p7zip || { echo "ERROR: 7z required for --english"; exit 1; }
  SZ=$(command -v 7z || command -v 7za)

  TMP=$(mktemp -d)
  trap 'rm -rf "$TMP"' EXIT
  URL="https://files.catbox.moe/khms20.7z"
  echo "    downloading $URL"
  curl -L -o "$TMP/en.7z" "$URL"

  "$SZ" x -y "$TMP/en.7z" -o"$TMP" >/dev/null
  INNER="$TMP/Latest version/_Grok 4 MTL + AutoWrap for v1_20.7z"
  if [ ! -f "$INNER" ]; then echo "ERROR: expected inner archive not found"; exit 1; fi
  "$SZ" x -y "$INNER" -o"$TMP/v120" >/dev/null

  PATCH_XP3="$TMP/v120/patch2.xp3"
  if [ ! -f "$PATCH_XP3" ]; then echo "ERROR: patch2.xp3 not found in archive"; exit 1; fi

  if [ -f "$GAMEDIR/patch2.xp3" ]; then
    BAK="$GAMEDIR/patch2.xp3.bak"
    [ -f "$BAK" ] && BAK="$GAMEDIR/patch2.xp3.bak.$(date +%s)"
    cp -f "$GAMEDIR/patch2.xp3" "$BAK"
    echo "    backed up existing patch2.xp3 -> $BAK"
  fi
  cp -f "$PATCH_XP3" "$GAMEDIR/patch2.xp3"
  echo "    English patch2.xp3 installed."
  echo "    (KiriKiri auto-mounts *.xp3, so the translation loads automatically.)"
fi

# --- 3) optional Heroic override --------------------------------------------
if [ "$HEROIC" -eq 1 ]; then
  echo "==> Configuring Heroic WINEDLLOVERRIDES=version=n"
  HC=~/.config/heroic/GamesConfig
  if [ ! -d "$HC" ]; then echo "    Heroic config dir not found ($HC) — set manually"; else
    FOUND=0
    for cfg in "$HC"/*.json; do
      [ -e "$cfg" ] || continue
      if grep -q "$GAMEDIR" "$cfg" 2>/dev/null; then
        FOUND=1
        if command -v python3 >/dev/null 2>&1; then
          python3 - "$cfg" "$GAMEDIR" <<'PY'
import json, os, sys
cfg, gdir = sys.argv[1], os.path.abspath(sys.argv[2])
try:
    d = json.load(open(cfg))
except Exception:
    sys.exit(0)
ex = d.get("exePath", "")
if gdir not in ex and os.path.basename(gdir) not in ex:
    sys.exit(0)
w = d.get("WINEDLLOVERRIDES", "") or ""
if "version" not in w:
    w = (w + "," if w else "") + "version=n"
    d["WINEDLLOVERRIDES"] = w
    json.dump(d, open(cfg, "w"), indent=2)
    print("    updated", cfg, "-> WINEDLLOVERRIDES=", w)
PY
        else
          echo "    python3 not found — set WINEDLLOVERRIDES=version=n manually in $cfg"
        fi
      fi
    done
    [ "$FOUND" -eq 0 ] && echo "    no Heroic config matched this game dir — set manually"
  fi
else
  echo "==> Reminder: in Heroic, set the game's environment override:"
  echo "    WINEDLLOVERRIDES=version=n"
fi

echo
echo "==> Done. Launch the game (via Heroic / your wine prefix)."
