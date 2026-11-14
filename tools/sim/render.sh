#!/bin/sh
# Renders UI previews into docs/images (PNG, 2x scaled).
# Usage: tools/sim/render.sh [language index 0..4]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/sim"
mkdir -p "$OUT" "$ROOT/docs/images"
g++ -std=gnu++17 -O2 -DDSO_SIM -I"$ROOT/src" -o "$OUT/sim" \
	"$ROOT/tools/sim/sim.cpp" "$ROOT/src/ui.cpp" "$ROOT/src/gfx.cpp" \
	"$ROOT/src/measure.cpp" "$ROOT/src/model.cpp" "$ROOT/src/i18n.cpp"
"$OUT/sim" "$OUT" "${1:-0}"
python3 -I - "$OUT" "$ROOT/docs/images" <<'PY'
import sys, glob, os
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(os.path.join(src, "*.ppm"))):
    im = Image.open(p).resize((640, 480), Image.NEAREST)
    im.save(os.path.join(dst, os.path.basename(p)[:-4] + ".png"))
PY
echo "previews written to docs/images"
