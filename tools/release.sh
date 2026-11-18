#!/bin/sh
# Zverejní zostavenie z vetvy "build" do verejného repozitára Korgus-Download (automatické aktualizácie).
# Použitie: tools/release.sh "Poznámky k verzii"   (spúšťa sa z klonu Korgus-Player, Korgus-Download vedľa)
set -e
NOTES="$1"
DL=${DL:-../korgus-download}
TMP=$(mktemp -d)
git clone -q --depth 1 -b build "$(git remote get-url origin)" "$TMP/b"
V=$(cat "$TMP/b/version.txt" | tr -d '\r\n ')
mkdir -p "$DL/player"
cp "$TMP/b/Korgus Player.exe" "$DL/player/Korgus Player.exe"
if [ ! -f "$DL/player/libmpv-2.zip" ]; then
  git clone -q --depth 1 -b libmpv "$(git remote get-url origin)" "$TMP/m"
  cp "$TMP/m/libmpv-2.zip" "$DL/player/libmpv-2.zip"
fi
python3 - "$DL/latest-player.json" "$V" "$NOTES" <<'PY'
import json, sys
p, v, notes = sys.argv[1:4]
base = "https://raw.githubusercontent.com/Korgus7/Korgus-Download/main/player/"
json.dump({"version": v, "url": base + "Korgus%20Player.exe", "libmpv": base + "libmpv-2.zip", "notes": notes},
          open(p, "w"), ensure_ascii=False, indent=2)
PY
cd "$DL"
git add player latest-player.json
git commit -qm "Korgus Player $V"
git -c push.negotiate=true push -q origin HEAD
echo "Zverejnené $V"
